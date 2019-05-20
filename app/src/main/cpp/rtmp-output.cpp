
#include <inttypes.h>
# include "callback/calldata.h"
# include "callback/signal.h"
#include "util/platform.h"
#include "util/dstr.h"

# include <arpa/inet.h>
# include <sys/socket.h>
# include "rtmp-output.h"
# include "rtmp-encoder.h"
# include "rtmp-stream.h"
# include "rtmp-defs.h"

# include "rtmp-audio-output.h"
# include "rtmp-video-output.h"

#define MICROSECOND_DEN 1000000


static void interleave_packets(void *data, encoder_packet &packet)
{
	RtmpOutput *output = static_cast<RtmpOutput *>(data);
	if(!output)
		return;
	output->on_interleave_packets(packet);
}

RtmpOutput::RtmpOutput(std::shared_ptr<media_output> &v, std::shared_ptr<media_output> &a):
video(v),
audio(a),
video_offset(0),
audio_offset(0),
highest_audio_ts(0),
highest_video_ts(0),
stop_code(0),
total_frames(0),
scaled_width(0),
scaled_height(0),
received_video(false),
received_audio(false),
data_active(false),
end_data_capture_thread_active(false),
active(false),
video_conversion_set(false),
audio_conversion_set(false),
valid(false),
status(RTMP_STREAM_INIT),
stopping_event(NULL),
status_semaphore(NULL)
{
	pthread_mutex_init_value(&interleaved_mutex);

	if (pthread_mutex_init(&interleaved_mutex, NULL) != 0)
		return;
	if (pthread_mutex_init(&status_mutex, NULL) != 0)
		return;
	if (os_event_init(&stopping_event, OS_EVENT_TYPE_MANUAL) != 0)
		return;
	if (os_sem_init(&status_semaphore, 0) != 0)
		return;

	if (pthread_create(&signal_notify_thread, NULL, rtmp_status_update, this) != 0)
		return;

	os_event_signal(stopping_event);
	valid = true;
}

RtmpOutput::~RtmpOutput()
{
    destroy();

    os_event_destroy(stopping_event);
    pthread_mutex_destroy(&interleaved_mutex);
	pthread_mutex_destroy(&status_mutex);

	valid = false;
	os_sem_post(status_semaphore);
	void *thread_ret = NULL;
	pthread_join(signal_notify_thread, &thread_ret);

    if (!last_error_message.empty())
        last_error_message.clear();
}

std::shared_ptr<media_encoder> RtmpOutput::get_video_encoder()
{
	return video_encoder.lock();
}

std::shared_ptr<media_encoder> RtmpOutput::get_audio_encoder()
{
	return audio_encoder.lock();
}

bool RtmpOutput::begin_data_capture()
{
	bool encoded, has_video, has_audio;

	if (is_active()) return false;

	total_frames   = 0;

	convert_flags(encoded, has_video, has_audio);

	if (!can_begin_data_capture(encoded, has_video, has_audio))
		return false;

	os_atomic_set_bool(&data_active, true);
	hook_data_capture(has_video, has_audio);

	do_output_signal(RTMP_STREAM_ACTIVE);
	os_atomic_set_bool(&active, true);

	do_output_signal(RTMP_STREAM_START);

	return true;
}

void RtmpOutput::end_data_capture()
{
	end_data_capture_internal(true);
}

void RtmpOutput::set_last_error(const char *message)
{
	if (!last_error_message.empty())
		last_error_message.clear();

	if (message)
		last_error_message = message;
}

bool RtmpOutput::can_begin_data_capture()
{
	bool encoded, has_video, has_audio;

	if (is_active()) return false;

	if (os_atomic_load_bool(&end_data_capture_thread_active))
		pthread_join(end_data_capture_thread, NULL);

	convert_flags(encoded, has_video, has_audio);

	return can_begin_data_capture(encoded, has_video, has_audio);
}

bool RtmpOutput::can_begin_data_capture(bool encoded, bool has_video,
		bool has_audio)
{
	if (has_video) {
		if (encoded) {
			if (!video_encoder.lock())
				return false;
		} else {
			if (!video.lock())
				return false;
		}
	}

	if (has_audio) {
		if (!audio_valid(encoded)) {
			return false;
		}
	}

	return true;
}

bool RtmpOutput::initialize_encoders()
{
	bool encoded, has_video, has_audio;

	if (is_active()) return false;

	convert_flags(encoded, has_video, has_audio);

	if (!encoded)
		return false;
	if (has_video && !video_encoder.lock()->initialize())
		return false;
	if (has_audio && !audio_encoder.lock()->initialize())
		return false;

	if (has_video && has_audio)
		pair_encoders();

	return true;
}

void  RtmpOutput::end_data_capture_internal(bool signal)
{
	int ret;

	if (!is_active() || !is_data_active()) {
		if (signal) {
			signal_stop();
			stop_code = OBS_OUTPUT_SUCCESS;
			os_event_signal(stopping_event);
		}
		return;
	}

	os_atomic_set_bool(&data_active, false);

	if (os_atomic_load_bool(&end_data_capture_thread_active))
		pthread_join(end_data_capture_thread, NULL);

	os_atomic_set_bool(&end_data_capture_thread_active, true);
	ret = pthread_create(&end_data_capture_thread, NULL,
						 end_data_capture_thread_fun, this);
	if (ret != 0) {
		end_data_capture_thread_fun(this);
	}

	if (signal) {
		signal_stop();
		stop_code = OBS_OUTPUT_SUCCESS;
	}
}

void RtmpOutput::signal_stop(int code)
{
	stop_code = code;
    end_data_capture();
}

void RtmpOutput::signal_stop()
{
	calldata params;
	calldata_init(&params);
	calldata_set_string(&params, "last_error", last_error_message.c_str());
	calldata_set_int(&params, "code", stop_code);
	calldata_set_ptr(&params, "output", this);

	calldata_free(&params);
}

void RtmpOutput::convert_flags(bool &encoded, bool &has_video, bool &has_audio)
{
	encoded = (flags & OBS_OUTPUT_ENCODED) != 0;
	has_video = (flags & OBS_OUTPUT_VIDEO) != 0;
	has_audio = (flags & OBS_OUTPUT_AUDIO) != 0;
}

void RtmpOutput::hook_data_capture(bool has_video, bool has_audio)
{
	pthread_mutex_lock(&interleaved_mutex);
	reset_packet_data();
	pthread_mutex_unlock(&interleaved_mutex);

	if (has_audio)
		start_audio_encoders(interleave_packets);
	if (has_video)
		video_encoder.lock()->start(interleave_packets,this);
}

void RtmpOutput::do_output_signal(int code_def)
{
	pthread_mutex_lock(&status_mutex);
	status = code_def;
	pthread_mutex_unlock(&status_mutex);
	os_sem_post(status_semaphore);
}

bool RtmpOutput::audio_valid(bool encoded)
{
	if (encoded) {
		if (!audio_encoder.lock()) {
			return false;
		}
	} else {
		if (!audio.lock())
			return false;
	}

	return true;
}

void RtmpOutput::pair_encoders()
{
	std::shared_ptr<media_encoder> ve = video_encoder.lock();
	std::shared_ptr<media_encoder> ae = audio_encoder.lock();
	if (ve && ae) {
		pthread_mutex_lock(&ae->init_mutex);
		pthread_mutex_lock(&ve->init_mutex);

		if (!ae->active && !ve->active &&
			!ve->paired_encoder.lock() && !ae->paired_encoder.lock()) {

			ae->paired_encoder = video_encoder;
			ve->paired_encoder = audio_encoder;
		}

		pthread_mutex_unlock(&ve->init_mutex);
		pthread_mutex_unlock(&ae->init_mutex);
	}
}

void *RtmpOutput::end_data_capture_thread_fun(void *data)
{
	bool encoded, has_video, has_audio;
	RtmpOutput *output = (RtmpOutput *)data;

	output->convert_flags(encoded, has_video, has_audio);

	if (has_video)
		output->video_encoder.lock()->stop(interleave_packets, output);
	if (has_audio)
		output->stop_audio_encoders(interleave_packets);

	output->do_output_signal(RTMP_STREAM_DEACTIVE);
	LOGI("end_data_capture_thread_fun--------------------------------------------------!!!!!!!!!!!!!!!!!!!!!!!!!!!! ");
	os_atomic_set_bool(&output->active, false);
	os_event_signal(output->stopping_event);
	os_atomic_set_bool(&output->end_data_capture_thread_active, false);

	return NULL;
}

void RtmpOutput::reset_packet_data()
{
	received_audio   = false;
	received_video   = false;
	highest_audio_ts = 0;
	highest_video_ts = 0;
	video_offset     = 0;

	audio_offset = 0;

	free_packets();
}

void RtmpOutput::start_audio_encoders(encoded_callback_t encoded_callback)
{
	audio_encoder.lock()->start(encoded_callback,this);
}

void RtmpOutput::stop_audio_encoders(encoded_callback_t encoded_callback)
{
    audio_encoder.lock()->stop(encoded_callback,this);
}

struct video_scale_info* RtmpOutput::get_video_conversion()
{
	if (video_conversion_set) {
		if (!video_conversion.width)
			video_conversion.width = video_get_width();

		if (!video_conversion.height)
			video_conversion.height = video_get_height();

		return &video_conversion;

	} else if (has_scaling()) {

		std::shared_ptr<VideoOutput> video_output =
				std::dynamic_pointer_cast<VideoOutput>(video.lock());

		const video_output_info *info = video_output->get_info();

		video_conversion.format     = info->format;
		video_conversion.colorspace = VIDEO_CS_DEFAULT;
		video_conversion.range      = VIDEO_RANGE_DEFAULT;
		video_conversion.width      = scaled_width;
		video_conversion.height     = scaled_height;
		return &video_conversion;
	}

	return NULL;
}

uint32_t RtmpOutput::video_get_width()
{
	if ((flags & OBS_OUTPUT_VIDEO) == 0)
		return 0;

	if (flags & OBS_OUTPUT_ENCODED){
        std::shared_ptr<X264Encoder> vencoder =
                std::dynamic_pointer_cast<X264Encoder>(video_encoder.lock());
		return vencoder->get_width();
	}
	else{
		std::shared_ptr<VideoOutput> video_output =
				std::dynamic_pointer_cast<VideoOutput>(video.lock());

		if(!video_output)
			return 0;

		return scaled_width != 0 ?
			   scaled_width :video_output->get_width();
	}

}

uint32_t RtmpOutput::video_get_height()
{
	if ((flags & OBS_OUTPUT_VIDEO) == 0)
		return 0;

	if (flags & OBS_OUTPUT_ENCODED){
        std::shared_ptr<X264Encoder> vencoder =
                std::dynamic_pointer_cast<X264Encoder>(video_encoder.lock());
		return vencoder->get_height();
	}
	else{
		std::shared_ptr<VideoOutput> video_output =
				std::dynamic_pointer_cast<VideoOutput>(video.lock());

		if(!video_output)
			return 0;

		return scaled_height != 0 ?
			   scaled_height : video_output->get_height();
	}

}

bool RtmpOutput::actual_start()
{
	os_event_wait(stopping_event);
	stop_code = 0;
	if (!last_error_message.empty())
		last_error_message.clear();

	return start();
}

bool RtmpOutput::output_start()
{
	if (actual_start()) {
		do_output_signal(RTMP_STREAM_STARTING);
		return true;
	}
	return false;
}

bool RtmpOutput::output_active()
{
    return is_active();
}

void RtmpOutput::output_stop()
{
    if (!is_active())
        return;
    if (!stopping()) {
        do_output_signal(RTMP_STREAM_STOPPING);
        actual_stop(false);
    }

    std::shared_ptr<media_output> video_output = video.lock();
    if(video_output)
        video_output->output_close();

    std::shared_ptr<media_output> audio_output = audio.lock();
    if(audio_output)
        audio_output->output_close();
}

bool RtmpOutput::is_data_active()
{
    return os_atomic_load_bool(&data_active);
}

void RtmpOutput::set_video_encoder(std::shared_ptr<media_encoder> &encoder)
{
    if (encoder && encoder->type != OBS_ENCODER_VIDEO)
        return;

    if (video_encoder.lock().get() == encoder.get()) return;
    video_encoder = encoder;
	video_encoder.lock()->set_output(std::dynamic_pointer_cast<rtmp_output_base>(shared_from_this()));

    if (scaled_width && scaled_height){
		std::shared_ptr<X264Encoder> vencoder =
				std::dynamic_pointer_cast<X264Encoder>(video_encoder.lock());
		vencoder->set_scaled_size(scaled_width, scaled_height);
    }
}

void RtmpOutput::set_audio_encoder(std::shared_ptr<media_encoder> &encoder)
{
    if (encoder && encoder->type != OBS_ENCODER_AUDIO)
        return;

    if (audio_encoder.lock().get() == encoder.get()) return;
    audio_encoder = encoder;
	audio_encoder.lock()->set_output(std::dynamic_pointer_cast<rtmp_output_base>(shared_from_this()));
}

void RtmpOutput::remove_encoder(std::shared_ptr<media_encoder> encoder)
{
	if (video_encoder.lock().get() == encoder.get()) {
		video_encoder.reset();
	} else if(audio_encoder.lock().get() == encoder.get()){
		audio_encoder.reset();
	}
}

bool RtmpOutput::has_scaling()
{
	std::shared_ptr<VideoOutput> video_output =
			std::dynamic_pointer_cast<VideoOutput>(video.lock());

	uint32_t video_width  = video_output->get_width();
	uint32_t video_height = video_output->get_height();

	return scaled_width && scaled_height &&
		   (video_width  != scaled_width ||
			video_height != scaled_height);
}

void RtmpOutput::force_stop()
{
	if (!stopping()) {
		stop_code = 0;
		do_output_signal(RTMP_STREAM_STOPPING);
	}
	actual_stop(true);
}

void RtmpOutput::actual_stop(bool force)
{
	if (stopping() && !force)
		return;
	os_event_reset(stopping_event);
	stop();
}

void RtmpOutput::destroy()
{
	if (valid && is_active())
		actual_stop(true);

	os_event_wait(stopping_event);
	if (os_atomic_load_bool(&end_data_capture_thread_active))
		pthread_join(end_data_capture_thread, NULL);

	stream_destroy();

	free_packets();

	if (video_encoder.lock())
		video_encoder.lock()->remove_output();

	if (audio_encoder.lock())
		audio_encoder.lock()->remove_output();
}

void RtmpOutput::on_interleave_packets(encoder_packet &packet)
{
	encoder_packet out;

	if (!is_active())
		return;

    pthread_mutex_lock(&interleaved_mutex);

    packet.create_instance(out);



	if (packet.type == OBS_ENCODER_AUDIO)
		packet.track_idx = 0;

	if (!received_video &&
		packet.type == OBS_ENCODER_VIDEO &&
		!packet.keyframe) {
		discard_unused_audio_packets(packet.dts_usec);
		pthread_mutex_unlock(&interleaved_mutex);
		LOGI("on_interleave_packets-------------------- discard_unused_audio_packets received_video %d, packet.type %d, packet.keyframe %d",received_video,packet.type,packet.keyframe );
		return;
	}

    bool was_started = received_audio && received_video;
    if (was_started)
        apply_interleaved_packet_offset(out);
    else
        check_received(packet);

    insert_interleaved_packet(out);

    set_higher_ts(out);

    if(!need_sync_packet())
        send_interleaved();
    else{

        if (received_audio && received_video) {
            LOGI("on_interleave_packets-------------------- was_started : %d ",was_started);
            if (!was_started) {
                if (prune_interleaved_packets()) {
                    if (initialize_interleaved_packets()) {
                        resort_interleaved_packets();
                        send_interleaved();
                    }
                }
            } else {
                send_interleaved();
            }
        }
    }

	pthread_mutex_unlock(&interleaved_mutex);
}

bool RtmpOutput::has_higher_opposing_ts(encoder_packet &packet)
{
	if (packet.type == OBS_ENCODER_VIDEO)
        return highest_audio_ts > packet.dts_usec;
	else
        return highest_video_ts > packet.dts_usec;
}

void RtmpOutput::send_interleaved()
{
    if(interleaved_packets.size() == 0)
        return;

	encoder_packet out = interleaved_packets[0];
/*
	if (!has_higher_opposing_ts(out))
		return;
*/
	interleaved_packets.erase(interleaved_packets.begin());

	if (out.type == OBS_ENCODER_VIDEO)
		total_frames++;
	LOGI("send_interleaved : ---------------------------------------------------- %d ", out.data.size());
	encoded_packet(out);
}

void RtmpOutput::discard_unused_audio_packets(int64_t dts_usec)
{
	size_t idx = 0;
	size_t packet_size = interleaved_packets.size();
	for (; idx < packet_size; idx++) {
		if (interleaved_packets[idx].dts_usec >= dts_usec)
			break;
	}

	if (idx)
		discard_to_idx(idx);
}

void RtmpOutput::apply_interleaved_packet_offset(encoder_packet &out)
{
	int64_t offset;

	offset = (out.type == OBS_ENCODER_VIDEO) ?
			 video_offset : audio_offset;

	out.dts -= offset;
	out.pts -= offset;
	out.dts_usec = out.get_dts_usec();
}

void RtmpOutput::discard_to_idx(size_t idx)
{
	size_t buffer_size = interleaved_packets.size();
	if(idx < buffer_size)
		interleaved_packets.erase(interleaved_packets.begin(),
								  interleaved_packets.begin()+idx);
}

void RtmpOutput::check_received(encoder_packet &out)
{
	if (out.type == OBS_ENCODER_VIDEO) {
		if (!received_video)
            received_video = true;
	} else {
		if (!received_audio)
			received_audio = true;
	}
}

void RtmpOutput::insert_interleaved_packet(encoder_packet &out)
{
	size_t idx;
	for (idx = 0; idx < interleaved_packets.size(); idx++) {
		if (out.dts_usec == interleaved_packets[idx].dts_usec &&
			out.type == OBS_ENCODER_VIDEO) {
			break;
		} else if (out.dts_usec < interleaved_packets[idx].dts_usec) {
			break;
		}
	}
	interleaved_packets.insert(interleaved_packets.begin()+idx,out);
}

void RtmpOutput::set_higher_ts(encoder_packet &packet)
{
	if (packet.type == OBS_ENCODER_VIDEO) {
		if (highest_video_ts < packet.dts_usec)
			highest_video_ts = packet.dts_usec;
	} else {
		if (highest_audio_ts < packet.dts_usec)
			highest_audio_ts = packet.dts_usec;
	}
}

bool RtmpOutput::prune_interleaved_packets()
{
	size_t start_idx = 0;
	int prune_start = prune_premature_packets();

	/* prunes the first video packet if it's too far away from audio */
	if (prune_start == -1)
		return false;
	else if (prune_start != 0)
		start_idx = (size_t)prune_start;
	else
		start_idx = get_interleaved_start_idx();

	if (start_idx)
		discard_to_idx(start_idx);

	return true;
}

int RtmpOutput::prune_premature_packets()
{
	int video_idx = find_first_packet_type_idx(OBS_ENCODER_VIDEO);
	if (video_idx == -1) {
		received_video = false;
		return -1;
	}

    int max_idx = video_idx;
	encoder_packet &packet = interleaved_packets[video_idx];
    int64_t duration_usec = packet.timebase_num*1000000LL / packet.timebase_den;
	int audio_idx = find_first_packet_type_idx(OBS_ENCODER_AUDIO);
	if (audio_idx == -1) {
		received_audio = false;
		return -1;
	}
	if (audio_idx > max_idx)
		max_idx = audio_idx;

    int64_t diff = interleaved_packets[audio_idx].dts_usec - interleaved_packets[video_idx].dts_usec;
	return video_idx;//diff > duration_usec ? max_idx + 1 : 0;
}

int RtmpOutput::find_last_packet_type_idx(enum obs_encoder_type type)
{
	for (size_t i = interleaved_packets.size(); i > 0; i--) {
		if (interleaved_packets[i - 1].type == type)
			return (int)(i - 1);
	}

	return -1;
}

int RtmpOutput::find_first_packet_type_idx(obs_encoder_type type)
{
	for (size_t i = 0; i < interleaved_packets.size(); i++) {
		if (interleaved_packets[i].type == type)
			return (int)i;
	}
	return -1;
}

bool RtmpOutput::find_first_packet_type(encoder_packet &packet, obs_encoder_type type)
{
	int idx = find_first_packet_type_idx(type);
	if(idx == -1)
		return false;
	packet = interleaved_packets[idx];
	return true;
}

size_t RtmpOutput::get_interleaved_start_idx()
{
	int64_t closest_diff = 0x7FFFFFFFFFFFFFFFLL;
	encoder_packet first_video;
	if(!find_first_packet_type(first_video,OBS_ENCODER_VIDEO))
		return DARRAY_INVALID;
	size_t video_idx = find_first_packet_type_idx(OBS_ENCODER_VIDEO);
	size_t idx = 0;

	int packets_size = interleaved_packets.size();
	for (size_t i = 0; i < packets_size; i++) {
		encoder_packet &packet = interleaved_packets[i];
		int64_t diff = llabs(packet.dts_usec - first_video.dts_usec);
		if (diff < closest_diff) {
			closest_diff = diff;
			idx = i;
		}
	}

	return video_idx < idx ? video_idx : idx;
}

bool RtmpOutput::initialize_interleaved_packets()
{
	encoder_packet video;
	encoder_packet audio;
	encoder_packet last_audio;
	size_t start_idx;

	if (!get_audio_and_video_packets(video, audio))
		return false;

	find_last_packet_type(last_audio, OBS_ENCODER_AUDIO);

	if (last_audio.dts_usec < video.dts_usec) {
		received_audio = false;
		return false;
	}

	start_idx = get_interleaved_start_idx();
	if (start_idx) {
		discard_to_idx(start_idx);
		if (!get_audio_and_video_packets(video, audio))
			return false;
	}

	video_offset = video.pts;
	audio_offset = audio.dts;

	highest_audio_ts -= audio.dts_usec;
	highest_video_ts -= video.dts_usec;

	/* apply new offsets to all existing packet DTS/PTS values */
	for (size_t i = 0; i < interleaved_packets.size(); i++) {
		apply_interleaved_packet_offset(interleaved_packets[i]);
	}

	return true;
}

bool RtmpOutput::get_audio_and_video_packets(encoder_packet &video,
										encoder_packet &audio)
{
	if (!find_first_packet_type(video,OBS_ENCODER_VIDEO)){
		received_video = false;
		return false;
	}

	if (!find_first_packet_type(audio, OBS_ENCODER_AUDIO)) {
		received_audio = false;
		return false;
	}

	return true;
}

bool RtmpOutput::find_last_packet_type(encoder_packet &packet, obs_encoder_type type)
{
	int idx = find_last_packet_type_idx(type);
	if(idx == -1)
		return false;
	packet = interleaved_packets[idx];
	return true;
}

void RtmpOutput::resort_interleaved_packets()
{
	std::vector<encoder_packet> old_array = interleaved_packets;
	interleaved_packets.clear();

	for (size_t i = 0; i < old_array.size(); i++)
		insert_interleaved_packet(old_array[i]);
}

void RtmpOutput::free_packets()
{
	interleaved_packets.clear();
}

bool RtmpOutput::is_active()
{
    return os_atomic_load_bool(&active);
}

void *RtmpOutput::rtmp_status_update(void *param)
{
	RtmpOutput *rtmp_output = (RtmpOutput *)param;

	os_set_thread_name("rtmp-output: status monitor thread ");

	while (os_sem_wait(rtmp_output->status_semaphore) == 0) {
		if (!rtmp_output->valid)
			break;

		rtmp_output->update_status();
	}

	return NULL;
}

void RtmpOutput::update_status()
{
	pthread_mutex_lock(&status_mutex);
	int currrent_status = status;
	pthread_mutex_unlock(&status_mutex);

	if(currrent_status == RTMP_STREAM_DEACTIVE)
		output_stop();
}

bool RtmpOutput::need_sync_packet()
{
	bool encoded, has_video, has_audio;
	convert_flags(encoded, has_video, has_audio);

	if(has_video && has_audio)
		return true;

	return false;
}

