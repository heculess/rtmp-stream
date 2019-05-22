/******************************************************************************
    Copyright (C) 2013-2014 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/
# include <string>
# include "rtmp-encoder.h"
#include "util/dstr.h"
#include "util/threading.h"
# include "rtmp-defs.h"

#include "rtmp-output-base.h"
#include "rtmp-output.h"

static const char *do_encode_name = "do_encode";

media_encoder::media_encoder():
type(OBS_ENCODER_AUDIO),
cur_pts(0),
timebase_num(0),
timebase_den(0),
first_raw_ts(0),
start_ts(0),
offset_usec(0),
caps(0),
active(false),
initialized(false),
first_received(false),
destroy_on_stop(false)
{
	pthread_mutexattr_t attr;

	pthread_mutex_init_value(&init_mutex);
	pthread_mutex_init_value(&callbacks_mutex);
	pthread_mutex_init_value(&outputs_mutex);

	if (pthread_mutexattr_init(&attr) != 0)
		return;
	if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0)
		return;
	if (pthread_mutex_init(&init_mutex, &attr) != 0)
		return;
	if (pthread_mutex_init(&callbacks_mutex, &attr) != 0)
		return;
	if (pthread_mutex_init(&outputs_mutex, NULL) != 0)
		return;
}

media_encoder::~media_encoder()
{
    pthread_mutex_lock(&init_mutex);
    pthread_mutex_lock(&callbacks_mutex);
    bool destroy = callbacks.size() == 0;
    if (!destroy)
        destroy_on_stop = true;
    pthread_mutex_unlock(&callbacks_mutex);
    pthread_mutex_unlock(&init_mutex);

    if (destroy)
        actually_destroy();
}

bool media_encoder::initialize()
{
	bool success;

	pthread_mutex_lock(&init_mutex);
	success = initialize_internal();
	pthread_mutex_unlock(&init_mutex);

	return success;
}

bool media_encoder::initialize_internal()
{
	if (encoder_active())
		return true;
	if (initialized)
		return true;

	shutdown();
	on_initialize_internal();

	initialized = true;
	return true;
}

void media_encoder::set_output(std::shared_ptr<rtmp_output_base> op)
{
	pthread_mutex_lock(&outputs_mutex);
	output = op;
	pthread_mutex_unlock(&outputs_mutex);
}

void media_encoder::remove_output()
{
	pthread_mutex_lock(&outputs_mutex);
	output.reset();
	pthread_mutex_unlock(&outputs_mutex);
}

void media_encoder::do_encode(encoder_frame &frame)
{
	encoder_packet pkt;
	bool received = false;
	pkt.timebase_num = timebase_num;
	pkt.timebase_den = timebase_den;

	bool success = encode(frame, pkt, received);
	send_off_encoder_packet(success, received, pkt);
}

void media_encoder::full_stop()
{
	pthread_mutex_lock(&callbacks_mutex);
	callbacks.clear();
	remove_connection();
	pthread_mutex_unlock(&callbacks_mutex);
}

void media_encoder::shutdown()
{
	pthread_mutex_lock(&init_mutex);

	paired_encoder.reset();
	first_received  = false;
	offset_usec     = 0;
	start_ts        = 0;

	pthread_mutex_unlock(&init_mutex);
}

void media_encoder::remove_connection()
{
	on_remove_connection();
	shutdown();
	set_encoder_active(false);
}

void media_encoder::send_off_encoder_packet(bool success, bool received, encoder_packet &pkt)
{
	if (!success) {
		full_stop();
		return;
	}

	if (received) {
		if (!first_received) {
			offset_usec = pkt.get_dts_usec();
			first_received = true;
		}

		/* we use system time here to ensure sync with other encoders,
		 * you do not want to use relative timestamps here */
		pkt.dts_usec = start_ts / 1000 +
                pkt.get_dts_usec() - offset_usec;
		pkt.sys_dts_usec = pkt.dts_usec;

		pthread_mutex_lock(&callbacks_mutex);

		for (size_t i = callbacks.size(); i > 0; i--) {
			send_packet(&callbacks[i-1], pkt);
		}

		pthread_mutex_unlock(&callbacks_mutex);
	}
}

void media_encoder::send_packet(struct encoder_callback *cb, encoder_packet &packet)
{
	/* include SEI in first video packet */
	if (type == OBS_ENCODER_VIDEO && !cb->sent_first_packet)
		send_first_video_packet(cb, packet);
	else
		cb->new_packet(cb->param, packet);
}

void media_encoder::send_first_video_packet(struct encoder_callback *cb,
										  encoder_packet &packet)
{
	/* always wait for first keyframe */
	if (!packet.keyframe)
		return;

	encoder_packet first_packet = packet;
	cb->new_packet(cb->param, first_packet);
	cb->sent_first_packet = true;
}

void media_encoder::start(void (*new_packet)(void *param, encoder_packet &packet),
						void *param)
{
	pthread_mutex_lock(&init_mutex);
	start_internal(new_packet, param);
	pthread_mutex_unlock(&init_mutex);
}

void media_encoder::start_internal(void (*new_packet)(void *param, encoder_packet &packet),
								 void *param)
{

	encoder_callback cb = {false, new_packet, param};
	bool first   = false;

	pthread_mutex_lock(&callbacks_mutex);

	first = (callbacks.size() == 0);

	size_t idx = get_callback_idx(new_packet, param);
	if (idx == DARRAY_INVALID)
		callbacks.push_back(cb);

	pthread_mutex_unlock(&callbacks_mutex);

	if (first) {
		cur_pts = 0;
		add_connection();
	}
}

size_t media_encoder::get_callback_idx(
		void (*new_packet)(void *param, encoder_packet &packet),void *param)
{
	for (size_t i = 0; i < callbacks.size(); i++) {
		if (callbacks[i].new_packet == new_packet
			&& callbacks[i].param == param)
			return i;
	}

	return DARRAY_INVALID;
}

void media_encoder::stop(void (*new_packet)(void *param, encoder_packet &packet),
					   void *param)
{
	bool destroyed;

	pthread_mutex_lock(&init_mutex);
	destroyed = stop_internal(new_packet, param);
	if (!destroyed)
		pthread_mutex_unlock(&init_mutex);
}

bool media_encoder::stop_internal(void (*new_packet)(void *param, encoder_packet &packet),
								void *param)
{
	bool   last = false;
	size_t idx;

	pthread_mutex_lock(&callbacks_mutex);

	idx = get_callback_idx(new_packet, param);
	if (idx != DARRAY_INVALID) {
		callbacks.erase(callbacks.begin()+idx);
		last = (callbacks.size() == 0);
	}

	pthread_mutex_unlock(&callbacks_mutex);

	if (last) {
		remove_connection();
		initialized = false;

		if (destroy_on_stop) {
			pthread_mutex_unlock(&init_mutex);
			actually_destroy();
			return true;
		}
	}

	return false;
}

void media_encoder::actually_destroy()
{
	pthread_mutex_lock(&outputs_mutex);
	output.lock()->remove_encoder(shared_from_this());
	pthread_mutex_unlock(&outputs_mutex);

	on_actually_destroy();

	callbacks.clear();
	pthread_mutex_destroy(&init_mutex);
	pthread_mutex_destroy(&callbacks_mutex);
	pthread_mutex_destroy(&outputs_mutex);

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static inline bool is_audio_planar(enum audio_format format)
{
	switch (format) {
		case AUDIO_FORMAT_U8BIT:
		case AUDIO_FORMAT_16BIT:
		case AUDIO_FORMAT_32BIT:
		case AUDIO_FORMAT_FLOAT:
			return false;

		case AUDIO_FORMAT_U8BIT_PLANAR:
		case AUDIO_FORMAT_FLOAT_PLANAR:
		case AUDIO_FORMAT_16BIT_PLANAR:
		case AUDIO_FORMAT_32BIT_PLANAR:
			return true;

		case AUDIO_FORMAT_UNKNOWN:
			return false;
	}

	return false;
}

static inline uint32_t get_audio_channels(enum speaker_layout speakers)
{
	switch (speakers) {
		case SPEAKERS_MONO:             return 1;
		case SPEAKERS_STEREO:           return 2;
		case SPEAKERS_2POINT1:          return 3;
		case SPEAKERS_4POINT0:          return 4;
		case SPEAKERS_4POINT1:          return 5;
		case SPEAKERS_5POINT1:          return 6;
		case SPEAKERS_7POINT1:          return 8;
		case SPEAKERS_UNKNOWN:          return 0;
	}

	return 0;
}

static inline size_t get_audio_bytes_per_channel(enum audio_format format)
{
	switch (format) {
		case AUDIO_FORMAT_U8BIT:
		case AUDIO_FORMAT_U8BIT_PLANAR:
			return 1;

		case AUDIO_FORMAT_16BIT:
		case AUDIO_FORMAT_16BIT_PLANAR:
			return 2;

		case AUDIO_FORMAT_FLOAT:
		case AUDIO_FORMAT_FLOAT_PLANAR:
		case AUDIO_FORMAT_32BIT:
		case AUDIO_FORMAT_32BIT_PLANAR:
			return 4;

		case AUDIO_FORMAT_UNKNOWN:
			return 0;
	}

	return 0;
}

static inline size_t get_audio_planes(enum audio_format format,
									  enum speaker_layout speakers)
{
	return (is_audio_planar(format) ? get_audio_channels(speakers) : 1);
}

static inline size_t get_audio_size(enum audio_format format,
									enum speaker_layout speakers, uint32_t frames)
{
	bool planar = is_audio_planar(format);

	return (planar ? 1 : get_audio_channels(speakers)) *
		   get_audio_bytes_per_channel(format) *
		   frames;
}

void audio_resampler_destroy(audio_resampler_t *rs)
{
	if (rs) {
		/*
		if (rs->context)
			swr_free(&rs->context);
		if (rs->output_buffer[0])
			av_freep(&rs->output_buffer[0]);
*/
		bfree(rs);
	}
}

static inline void audio_input_free(struct audio_input *input)
{
	//audio_resampler_destroy(input->resampler);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
encoder_packet_info::encoder_packet_info():
data_size(0),
pts(0),
dts(0),
timebase_num(0),
timebase_den(0),
dts_usec(0),
sys_dts_usec(0),
priority(0),
drop_priority(0),
track_idx(0),
type(OBS_ENCODER_AUDIO),
keyframe(false),
data_ptr(NULL)
{
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
encoder_packet::encoder_packet():
is_attach_info(true)
{
}

encoder_packet::encoder_packet(encoder_packet_info &info):
is_attach_info(true)
{
    serialize_from(info);
}

encoder_packet::~encoder_packet()
{
    packet_release();
}

void encoder_packet::packet_release()
{
    if(is_attach_info && data_ptr)
        bfree(data_ptr);

    data_ptr = NULL;
    data_size = 0;
    data.clear();
}

void encoder_packet::create_instance(encoder_packet &dst)
{
    dst = *this;
}

int32_t encoder_packet::get_ms_time(int64_t val)
{
	return (int32_t)(val * MILLISECOND_DEN / timebase_den);
}

int64_t encoder_packet::get_dts_usec()
{
	return dts * MICROSECOND_DEN / timebase_den;
}

encoder_packet_info *encoder_packet::serialize_to()
{
    data_size = get_serialize_size();

    do{
        if(data_size == 0)
            break;

        if(data_ptr)
            break;

        is_attach_info = false;
        data_ptr = (uint8_t *)bmemdup(&data[0], data_size);

    }while(false);

	return dynamic_cast<encoder_packet_info *>(this);
}

int64_t encoder_packet::get_serialize_size()
{
    return data.size();
}

void encoder_packet::serialize_from(encoder_packet_info &info)
{
    pts = info.pts;
    dts = info.dts;
    timebase_num = info.timebase_num;
    timebase_den = info.timebase_den;
    dts_usec = info.dts_usec;
    sys_dts_usec = info.sys_dts_usec;
    priority = info.priority;
    drop_priority = info.drop_priority;
    track_idx = info.track_idx;
    type = info.type;
    keyframe = info.keyframe;


    if(info.data_size > 0){
        is_attach_info = true;
        data.resize(info.data_size,0);
        memcpy(&data[0],info.data_ptr,data.size());
        data_size = info.data_size;
        data_ptr = info.data_ptr;
    }
}

























