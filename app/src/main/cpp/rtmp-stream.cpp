
#include <netdb.h>
#include <sys/ioctl.h>

#include "rtmp-stream.h"

#include "rtmp-output.h"
#include "rtmp-flv-packager.h"
# include "rtmp-video-output.h"

RtmpStream::RtmpStream():
sent_headers(false),
got_first_video(false),
connecting(false),
stream_active(false),
disconnected(false),
send_sem(NULL),
stop_event(NULL),
buffer_space_available_event(NULL),
buffer_has_data_event(NULL),
socket_available_event(NULL),
send_thread_signaled_exit(NULL),
start_dts_offset(0),
max_shutdown_time_sec(0),
stop_ts(0),
shutdown_timeout_ts(0),
drop_threshold_usec(0),
pframe_drop_threshold_usec(0),
min_priority(0),
congestion(0),
last_dts_usec(0),
total_bytes_sent(0),
dropped_frames(0)
{
    id = "rtmp_output";
    encoded_video_codecs = "h264";
    encoded_audio_codecs = "aac";
    flags =  OBS_OUTPUT_AV|OBS_OUTPUT_ENCODED|OBS_OUTPUT_MULTI_TRACK;

	pthread_mutex_init_value(&packets_mutex);
	RTMP_Init(&rtmp);

	do{
		if (pthread_mutex_init(&packets_mutex, NULL) != 0)
			break;
		if (os_event_init(&stop_event, OS_EVENT_TYPE_MANUAL) != 0)
			break;
		if (os_event_init(&buffer_space_available_event, OS_EVENT_TYPE_AUTO) != 0)
			break;
		if (os_event_init(&buffer_has_data_event, OS_EVENT_TYPE_AUTO) != 0)
			break;
		if (os_event_init(&socket_available_event, OS_EVENT_TYPE_AUTO) != 0)
			break;
		if (os_event_init(&send_thread_signaled_exit, OS_EVENT_TYPE_MANUAL) != 0)
			break;
		return;

	}while(false);
}

RtmpStream::~RtmpStream()
{
    os_event_destroy(stop_event);
    os_sem_destroy(send_sem);
    pthread_mutex_destroy(&packets_mutex);

    os_event_destroy(buffer_space_available_event);
    os_event_destroy(buffer_has_data_event);
    os_event_destroy(socket_available_event);
    os_event_destroy(send_thread_signaled_exit);
}

void RtmpStream::stream_destroy()
{
	if (stopping() && !isConnecting()) {
		pthread_join(send_thread, NULL);

	} else if (isConnecting() || is_stream_active()) {
		if (connecting)
			pthread_join(connect_thread, NULL);

		stop_ts = 0;
		os_event_signal(stop_event);

		if (is_stream_active()) {
			os_sem_post(send_sem);
			end_data_capture();
			pthread_join(send_thread, NULL);
		}
	}

	free_packets();
}

bool RtmpStream::start()
{
	if (!can_begin_data_capture())
		return false;
	if (!initialize_encoders())
		return false;

	os_atomic_set_bool(&connecting, true);
	return pthread_create(&connect_thread, NULL, connect_thread_fun,
						  this) == 0;
}

void RtmpStream::stop(uint64_t ts)
{
	if (stopping() && ts != 0)
		return;

	if (isConnecting())
		pthread_join(connect_thread, NULL);

	stop_ts = ts / 1000ULL;

	if (ts)
		shutdown_timeout_ts = ts + (uint64_t)max_shutdown_time_sec * 1000000000ULL;

	if (is_stream_active()) {
		os_event_signal(stop_event);
		if (stop_ts == 0)
			os_sem_post(send_sem);
	} else {
		signal_stop(OBS_OUTPUT_SUCCESS);
	}
}

void RtmpStream::encoded_packet(encoder_packet &packet)
{
	encoder_packet new_packet;
	bool added_packet = false;

	if (isDisconnected() || !is_stream_active())
		return;

	if (packet.type == OBS_ENCODER_VIDEO) {
		if (!got_first_video) {
			start_dts_offset = packet.get_ms_time(packet.dts);
			got_first_video = true;
		}
		new_packet = X264Encoder::parse_avc_packet(packet);
	} else {
        new_packet = packet;
	}

	pthread_mutex_lock(&packets_mutex);

	if (!isDisconnected()) {
		added_packet = (packet.type == OBS_ENCODER_VIDEO) ?
					   add_video_packet(new_packet) :
					   add_packet(new_packet);
	}

	pthread_mutex_unlock(&packets_mutex);

	if (added_packet)
		os_sem_post(send_sem);
	else
        new_packet.packet_release();
}

uint64_t RtmpStream::get_total_bytes()
{
	return total_bytes_sent;
}

float RtmpStream::get_congestion()
{
	return min_priority > 0 ? 1.0f : congestion;
}

int RtmpStream::get_connect_time_ms()
{
	return rtmp.connect_time_ms;
}

int RtmpStream::get_dropped_frames()
{
	return dropped_frames;
}

bool RtmpStream::stopping()
{
	return os_event_try(stop_event) != EAGAIN;
}

bool RtmpStream::isConnecting()
{
	return os_atomic_load_bool(&connecting);
}

bool RtmpStream::is_stream_active()
{
	return os_atomic_load_bool(&stream_active);
}

bool RtmpStream::isDisconnected()
{
	return os_atomic_load_bool(&disconnected);
}

void RtmpStream::free_packets()
{
	pthread_mutex_lock(&packets_mutex);
	while (packets.size) {
		encoder_packet_info packet_info;
		packets.pop_front(&packet_info, sizeof(encoder_packet_info));
	}
	pthread_mutex_unlock(&packets_mutex);
}

size_t RtmpStream::num_buffered_packets()
{
	return packets.size / sizeof(encoder_packet_info);
}

void * RtmpStream::connect_thread_fun(void *data)
{
	struct RtmpStream *stream = (RtmpStream *)(data);
	int ret;

	os_set_thread_name("rtmp-stream: connect_thread");

	if (!stream->init_connect()) {
		stream->signal_stop( OBS_OUTPUT_BAD_PATH);
		return NULL;
	}

	ret = stream->try_connect();

	if (ret != OBS_OUTPUT_SUCCESS)
		stream->signal_stop(ret);

	if (!stream->stopping())
		pthread_detach(stream->connect_thread);

	os_atomic_set_bool(&stream->connecting, false);
	return NULL;
}

bool RtmpStream::add_packet(encoder_packet &packet)
{
	packets.push_back(packet.serialize_to(), sizeof(encoder_packet_info));
	return true;
}

void RtmpStream::check_to_drop_frames(bool pframes)
{
	encoder_packet first;
	int64_t buffer_duration_usec;
	size_t num_packets = num_buffered_packets();
	const char *name = pframes ? "p-frames" : "b-frames";
	int priority = pframes ?
				   OBS_NAL_PRIORITY_HIGHEST : OBS_NAL_PRIORITY_HIGH;
	int64_t drop_threshold = pframes ?pframe_drop_threshold_usec :
							 drop_threshold_usec;

	if (num_packets < 5) {
		if (!pframes)
			congestion = 0.0f;
		return;
	}

	if (!find_first_video_packet(first))
		return;

	/* if the amount of time stored in the buffered packets waiting to be
	 * sent is higher than threshold, drop frames */
	buffer_duration_usec = last_dts_usec - first.dts_usec;

	if (!pframes) {
		congestion = (float)buffer_duration_usec /
							 (float)drop_threshold;
	}

	if (buffer_duration_usec > drop_threshold)
		drop_frames(name, priority, pframes);
}

void RtmpStream::set_output_error()
{
	const char *msg = NULL;
	switch (rtmp.last_error_code)
	{
		case ETIMEDOUT:
			msg = "ConnectionTimedOut";
			break;
		case EACCES:
			msg = "PermissionDenied";
			break;
		case ECONNABORTED:
			msg = "ConnectionAborted";
			break;
		case ECONNRESET:
			msg = "ConnectionReset";
			break;
		case HOST_NOT_FOUND:
			msg = "HostNotFound";
			break;
		case NO_DATA:
			msg = "NoData";
			break;
		case EADDRNOTAVAIL:
			msg = "AddressNotAvailable";
			break;
	}

	if (!msg) {
		switch (rtmp.last_error_code) {
			case -0x2700:
				msg = "SSLCertVerifyFailed";
				break;
		}
	}

	set_last_error( msg);
}

bool RtmpStream::find_first_video_packet(encoder_packet &first)
{
	size_t count = packets.size / sizeof(encoder_packet_info);

	for (size_t i = 0; i < count; i++) {
		encoder_packet_info *cur = (encoder_packet_info *)packets.get_data(i * sizeof(first));
		if (cur->type == OBS_ENCODER_VIDEO && !cur->keyframe) {
			first = encoder_packet(*cur);
			return true;
		}
	}

	return false;
}

bool RtmpStream::add_video_packet(encoder_packet &packet)
{
	check_to_drop_frames(false);
	check_to_drop_frames(true);

	if (packet.drop_priority < min_priority) {
		dropped_frames++;
		return false;
	} else {
		min_priority = 0;
	}

	last_dts_usec = packet.dts_usec;
	LOGI("add_video_packet------------------- packet size : %d",packet.data.size());
	return add_packet(packet);
}

void RtmpStream::drop_frames(const char *name, int highest_priority, bool pframes)
{
	UNUSED_PARAMETER(pframes);

	circlebuffer new_buf;
	int num_frames_dropped = 0;

	UNUSED_PARAMETER(name);

	new_buf.reserve(sizeof(encoder_packet_info) * 8);

	while (packets.size) {
		encoder_packet_info packet_info;
		packets.pop_front(&packet_info, sizeof(encoder_packet_info));

		/* do not drop audio data or video keyframes */
		if (packet_info.type == OBS_ENCODER_AUDIO ||
			packet_info.drop_priority >= highest_priority) {
			new_buf.push_back( &packet_info, sizeof(encoder_packet_info));

		} else
			num_frames_dropped++;
	}

	packets.free();
	packets = new_buf;

	if (min_priority < highest_priority)
		min_priority = highest_priority;
	if (!num_frames_dropped)
		return;

	dropped_frames += num_frames_dropped;
}

int RtmpStream::init_send()
{
	int ret;
	reset_semaphore();

	ret = pthread_create(&send_thread, NULL, send_thread_fun, this);
	if (ret != 0) {
		RTMP_Close(&rtmp);
		return OBS_OUTPUT_ERROR;
	}

	os_atomic_set_bool(&stream_active, true);

	if (!send_meta_data()) {
		set_output_error();
		return OBS_OUTPUT_DISCONNECTED;
	}
	begin_data_capture();

	return OBS_OUTPUT_SUCCESS;
}

bool RtmpStream::reset_semaphore()
{
	os_sem_destroy(send_sem);
	return os_sem_init(&send_sem, 0) == 0;
}

bool RtmpStream::send_meta_data()
{

	std::shared_ptr<X264Encoder> vencoder =
			std::dynamic_pointer_cast<X264Encoder>(get_video_encoder());
	std::shared_ptr<aacEncoder> aencoder =
			std::dynamic_pointer_cast<aacEncoder>(get_audio_encoder());

    std::shared_ptr<VideoOutput> video =
            std::dynamic_pointer_cast<VideoOutput>(vencoder->media.lock());

	FLVPackager packager;
	if(vencoder){
		packager.setProperty("width",(double)vencoder->get_width());
		packager.setProperty("height",(double)vencoder->get_height());
	}
	packager.setProperty("videocodecid",7);

	if(video)
		packager.setProperty("framerate",video->get_frame_rate());

	packager.setProperty("audiocodecid",10);
	packager.setProperty("audiodatarate",0);
	packager.setProperty("audiosamplerate",(double)aencoder->get_sample_rate());

	std::vector<uint8_t> meta_data = packager.flv_meta_data(false);
    bool success = true;
    int data_size = meta_data.size();
	if (data_size > 0) {
		success = RTMP_Write(&rtmp, (char*)&meta_data[0],
                             data_size, 0) >= 0;
	}

	return success;
}

int RtmpStream::try_connect()
{
	if (path.empty())
		return OBS_OUTPUT_BAD_PATH;

	RTMP_Init(&rtmp);
	if (!RTMP_SetupURL(&rtmp, path.c_str()))
		return OBS_OUTPUT_BAD_PATH;

	RTMP_EnableWrite(&rtmp);

	encoder_name = "FMLE/3.0 (compatible; FMSc/1.0)";
	set_rtmp_str(&rtmp.Link.pubUser,   username.c_str());
	set_rtmp_str(&rtmp.Link.pubPasswd, password.c_str());
	set_rtmp_str(&rtmp.Link.flashVer,  encoder_name.c_str());
	rtmp.Link.swfUrl = rtmp.Link.tcUrl;
	memset(&rtmp.m_bindIP, 0, sizeof(rtmp.m_bindIP));

	RTMP_AddStream(&rtmp, key.c_str());

	rtmp.m_outChunkSize       = 4096;
	rtmp.m_bSendChunkSizeInfo = true;
	rtmp.m_bUseNagle          = true;

	if (!RTMP_Connect(&rtmp, NULL)) {
		set_output_error();
		return OBS_OUTPUT_CONNECT_FAILED;
	}

	if (!RTMP_ConnectStream(&rtmp, 0))
		return OBS_OUTPUT_INVALID_STREAM;

	return init_send();
}

bool RtmpStream::init_connect()
{
	int64_t drop_p;
	int64_t drop_b;

	if (stopping()) {
		pthread_join(send_thread, NULL);
	}

	free_packets();

	os_atomic_set_bool(&disconnected, false);
	total_bytes_sent = 0;
	dropped_frames   = 0;
	min_priority     = 0;
	got_first_video  = false;

	drop_b = 700;
	drop_p = 900;
	max_shutdown_time_sec = 30;

	if (drop_p < (drop_b + 200))
		drop_p = drop_b + 200;

	drop_threshold_usec = 1000 * drop_b;
	pframe_drop_threshold_usec = 1000 * drop_p;

	bind_ip = "default";

	return true;
}

void * RtmpStream::send_thread_fun(void *data)
{
	RtmpStream *stream = (RtmpStream *)data;

	os_set_thread_name("rtmp-stream: send_thread");

	while (os_sem_wait(stream->send_sem) == 0) {

		if (stream->stopping() && stream->stop_ts == 0)
			break;

		encoder_packet_info packet_info;
		if (!stream->get_next_packet(packet_info))
			continue;

		encoder_packet packet(packet_info);
		if (stream->stopping()) {
			if (stream->can_shutdown_stream(packet)) {
                packet.packet_release();
				break;
			}
		}

		if (!stream->sent_headers) {
			if (!stream->send_headers()) {
				os_atomic_set_bool(&stream->disconnected, true);
				break;
			}
		}

		if (stream->send_packet(packet, false, packet.track_idx) < 0) {
			os_atomic_set_bool(&stream->disconnected, true);
			break;
		}

	}

	stream->set_output_error();
	RTMP_Close(&stream->rtmp);

	if (!stream->stopping()) {
		pthread_detach(stream->send_thread);
		stream->signal_stop(OBS_OUTPUT_DISCONNECTED);
	} else {
		stream->end_data_capture();
	}

	stream->free_packets();
	os_event_reset(stream->stop_event);
	os_atomic_set_bool(&stream->stream_active, false);
	stream->sent_headers = false;
	return NULL;
}

bool RtmpStream::get_next_packet(encoder_packet_info &packet)
{
	bool new_packet = false;

	pthread_mutex_lock(&packets_mutex);
	if (packets.size) {
		packets.pop_front(&packet, sizeof(encoder_packet_info));
		new_packet = true;
	}
	pthread_mutex_unlock(&packets_mutex);

	return new_packet;
}

bool RtmpStream::can_shutdown_stream(encoder_packet &packet)
{
	uint64_t cur_time = os_gettime_ns();
	bool timeout = cur_time >= shutdown_timeout_ts;

	return timeout || packet.sys_dts_usec >= (int64_t)stop_ts;
}

bool RtmpStream::send_headers()
{
	sent_headers = true;

	if (!send_video_header())
		return false;
    if (!send_audio_header())
        return false;

	return true;
}

bool RtmpStream::send_audio_header()
{
	std::shared_ptr<aacEncoder> aencoder =
			std::dynamic_pointer_cast<aacEncoder>(get_audio_encoder());

	struct encoder_packet packet;
    packet.type = OBS_ENCODER_AUDIO;
    packet.timebase_den = 1;

	if (!aencoder)
		return true;

    packet.data = aencoder->get_encode_header();
	return send_packet(packet, true, 0) >= 0;
}

bool RtmpStream::send_video_header()
{
    std::shared_ptr<X264Encoder> vencoder =
            std::dynamic_pointer_cast<X264Encoder>(get_video_encoder());

	struct encoder_packet packet;
    packet.type = OBS_ENCODER_VIDEO;
    packet.timebase_den = 1;
    packet.keyframe = true;

    if (!vencoder)
        return true;

    packet.data = vencoder->get_encode_header();

	return send_packet(packet, true, 0) >= 0;
}

bool RtmpStream::discard_recv_data(size_t size)
{
	uint8_t buf[512];
	ssize_t ret;

	do {
		size_t bytes = size > 512 ? 512 : size;
		size -= bytes;
		ret = recv(rtmp.m_sb.sb_socket, buf, bytes, 0);

		if (ret <= 0) {
			int error = errno;
			return false;
		}
	} while (size > 0);

	return true;
}

int RtmpStream::send_packet(encoder_packet &packet, bool is_header, size_t idx)
{
	std::vector<uint8_t> send_data =
			FLVPackager::flv_packet_mux(packet, is_header ? 0 : start_dts_offset,is_header);

	int data_size = send_data.size();
    int ret = 0;
	if(data_size > 0)
        ret = RTMP_Write(&rtmp, (char*)&send_data[0], data_size, (int)idx);
    LOGI("send_packet------------------------------------------------------------ packet size : %d",data_size);
	total_bytes_sent += data_size;
	return ret;
}

void RtmpStream::set_rtmp_str(AVal *val, const char *str)
{
	bool valid  = (str && *str);
	val->av_val = valid ? (char*)str       : NULL;
	val->av_len = valid ? (int)strlen(str) : 0;
}
