

#include <stdio.h>
#include <stdint.h>
#include "util/platform.h"
#include "util/serializer.h"
#include "util/array-serializer.h"
# include "rtmp-encoder.h"
#include "rtmp-serialize-byte.h"
#include "rtmp-video-output.h"

#ifndef _STDINT_H_INCLUDED
#define _STDINT_H_INCLUDED
#endif

#define do_log(level, format, ...) \
	blog(level, "[x264 encoder: '%s'] " format, \
			obs_encoder_get_name(obsx264->encoder), ##__VA_ARGS__)

#define info(format, ...)  do_log(LOG_INFO,    format, ##__VA_ARGS__)
#define debug(format, ...) do_log(LOG_DEBUG,   format, ##__VA_ARGS__)

//#define ENABLE_VFR

static void receive_video(void *param, struct video_data *frame);

X264Encoder::X264Encoder():
preferred_format(VIDEO_FORMAT_NONE),
scaled_width(0),
scaled_height(0)
{
	id = "obs_x264";
	type = OBS_ENCODER_VIDEO;
	codec = "h264";
}

X264Encoder::~X264Encoder()
{

}

std::string X264Encoder::get_name()
{
	return "x264";
}

void X264Encoder::set_video(std::shared_ptr<media_output> &video)
{
	if (type != OBS_ENCODER_VIDEO) {
		return;
	}
	if (!video)
		return;

	video_output_info *voi =
	        std::dynamic_pointer_cast<VideoOutput>(video)->get_info();

	media        = video;
	timebase_num = voi->fps_den;
	timebase_den = voi->fps_num;
}

void X264Encoder::destroy()
{
	clear_data();
	delete this;
}

uint32_t X264Encoder::get_width()
{
	if (type != OBS_ENCODER_VIDEO) {
		return 0;
	}
	if (media.expired())
		return 0;

    std::shared_ptr<VideoOutput> video =
        std::dynamic_pointer_cast<VideoOutput>(media.lock());

	return scaled_width != 0 ?
		   scaled_width :
		   video->get_width();
}

uint32_t X264Encoder::get_height()
{
	if (type != OBS_ENCODER_VIDEO) {
		return 0;
	}
	if (media.expired())
		return 0;

    std::shared_ptr<VideoOutput> video =
            std::dynamic_pointer_cast<VideoOutput>(media.lock());

	return scaled_height != 0 ?
		   scaled_height :
		   video->get_height();
}

bool X264Encoder::encode(struct encoder_frame *frame,
				   encoder_packet &packet, bool *received_packet)
{
	if (!frame || !received_packet)
		return false;

	*received_packet = (frame->frames != 0);
	if (frame->frames){

		int frameType = frame->data[4] & 0x1F;

		packet.data		 = frame->data;
		packet.type          = OBS_ENCODER_VIDEO;
		packet.pts           = frame->pts;
		packet.dts           = frame->pts;
		packet.keyframe      = frameType == OBS_NAL_SLICE_IDR;

		LOGI("X264Encoder------------------- packet size : %d",packet.data.size());
	}

	return true;
}

bool X264Encoder::get_extra_data(std::vector<uint8_t> &data)
{
	if(extra_data.size() == 0)
		load_headers();

    data = extra_data;
	return true;
}

std::vector<uint8_t> X264Encoder::get_encode_header()
{
    std::vector<uint8_t> data;
    get_extra_data(data);

    std::vector<uint8_t> header;
    header = parse_header(data);
    return header;
}

bool X264Encoder::get_sei_data(std::vector<uint8_t> &sei_data)
{
	sei_data = sei;
	return true;
}

void X264Encoder::get_info(struct video_scale_info *info)
{
	enum video_format pref_format;

	pref_format = get_preferred_video_format();

	if (!valid_format(pref_format)) {
		pref_format = valid_format(info->format) ?
					  info->format : VIDEO_FORMAT_NV12;
	}

	info->format = pref_format;
}

void X264Encoder::load_headers()
{
    std::shared_ptr<VideoOutput> video =
            std::dynamic_pointer_cast<VideoOutput>(media.lock());
	if(!video)
		return;

	int	csdsize0 = video->format_csd0.size()-4;
	int	csdsize1 = video->format_csd1.size()-4;

	extra_data.resize(11 + csdsize0 + csdsize1,0);
	memcpy(&extra_data[8], (void*)&video->format_csd0[4], csdsize0);
	memcpy(&extra_data[11+csdsize0], (void*)&video->format_csd1[4], csdsize1);

	extra_data[0] = 0x01;
	extra_data[1] = extra_data[9];
	extra_data[2] = extra_data[10];
	extra_data[3] = extra_data[11];
	extra_data[4] =  0xFF;

	extra_data[5] = 0xE1;
	extra_data[6] = ((csdsize0 >> 8) & 0xFF);
	extra_data[7] = ((csdsize0) & 0xFF);

	int pos = 8 + csdsize0;
	extra_data[pos] = 0x01;
	extra_data[pos+1] = ((csdsize1 >> 8) & 0xFF);
	extra_data[pos+2] = ((csdsize1) & 0xFF);
}

bool X264Encoder::valid_format(enum video_format format)
{
	return format == VIDEO_FORMAT_I420 ||
		   format == VIDEO_FORMAT_NV12 ||
		   format == VIDEO_FORMAT_I444;
}

void X264Encoder::clear_data()
{
	sei.clear();
	extra_data.clear();
}

void X264Encoder::set_scaled_size(uint32_t width, uint32_t height)
{
	if (type != OBS_ENCODER_VIDEO)
		return;

	if (encoder_active())
		return;

	scaled_width  = width;
	scaled_height = height;
}

void X264Encoder::add_connection()
{
	video_scale_info info;
	get_video_info(&info);

    std::shared_ptr<VideoOutput> vo =
            std::dynamic_pointer_cast<VideoOutput>(media.lock());
	vo->start_raw_video(&info, receive_video,this);
}

void X264Encoder::on_remove_connection()
{
    std::shared_ptr<VideoOutput> vo =
            std::dynamic_pointer_cast<VideoOutput>(media.lock());
	vo->stop_raw_video(receive_video, this);
}

void X264Encoder::get_video_info(struct video_scale_info *info) {

    std::shared_ptr<VideoOutput> vo =
            std::dynamic_pointer_cast<VideoOutput>(media.lock());
	if(!vo)
		return;

	const video_output_info *voi = vo->get_info();

	info->format     = voi->format;
	info->colorspace = voi->colorspace;
	info->range      = voi->range;
	info->width      = get_width();
	info->height     = get_height();

	get_info(info);

	if (info->width != voi->width || info->height != voi->height)
		set_scaled_size(info->width, info->height);
}

static void receive_video(void *param, struct video_data *frame)
{
	X264Encoder *encoder = (X264Encoder *)param;
	std::shared_ptr<media_encoder> pair = encoder->paired_encoder.lock();
	struct encoder_frame  enc_frame;

	if (!encoder->first_received && pair) {
		if (!pair->first_received ||
			pair->first_raw_ts > frame->timestamp) {
			return;
		}
	}

	enc_frame.data    = frame->data;

	if (!encoder->start_ts)
		encoder->start_ts = frame->timestamp;

	enc_frame.frames = 1;
	enc_frame.pts    = encoder->cur_pts;

	encoder->do_encode(&enc_frame);

	encoder->cur_pts += encoder->timebase_num;
}

void X264Encoder::on_initialize_internal()
{

}

enum video_format X264Encoder::get_preferred_video_format()
{
	if (type != OBS_ENCODER_VIDEO)
		return VIDEO_FORMAT_NONE;

	return preferred_format;
}

std::vector<uint8_t> X264Encoder::parse_header(const std::vector<uint8_t> &data)
{
    SerializeByte serialize_byte;
	const uint8_t *sps = NULL, *pps = NULL;
	size_t sps_size = 0, pps_size = 0;

    std::vector<uint8_t> header;
    do{
        if (data.size() <= 6)
            break;

        if (!has_start_code(&data[0])) {
            header = data;
            break;
        }

        get_sps_pps(&data[0], data.size(), &sps, &sps_size, &pps, &pps_size);
        if (!sps || !pps || sps_size < 4)
            break;

        serialize_byte.write_uint8(0x01);
        serialize_byte.write(sps+1, 3);
        serialize_byte.write_uint8(0xff);
        serialize_byte.write_uint8(0xe1);

        serialize_byte.write_uint16((uint16_t)sps_size);
        serialize_byte.write(sps, sps_size);
        serialize_byte.write_uint8(0x01);
        serialize_byte.write_uint16((uint16_t)pps_size);
        serialize_byte.write(pps, pps_size);
        header = serialize_byte.GetDataByte();

    }while(false);

    return header;
}

void X264Encoder::get_sps_pps(const uint8_t *data, size_t size,
						const uint8_t **sps, size_t *sps_size,
						const uint8_t **pps, size_t *pps_size)
{
	const uint8_t *nal_start, *nal_end;
	const uint8_t *end = data+size;
	int type;

	nal_start = find_startcode(data, end);
	while (true) {
		while (nal_start < end && !*(nal_start++));

		if (nal_start == end)
			break;

		nal_end = find_startcode(nal_start, end);

		type = nal_start[0] & 0x1F;
		if (type == OBS_NAL_SPS) {
			*sps = nal_start;
			*sps_size = nal_end - nal_start;
		} else if (type == OBS_NAL_PPS) {
			*pps = nal_start;
			*pps_size = nal_end - nal_start;
		}

		nal_start = nal_end;
	}
}

const uint8_t * X264Encoder::find_startcode(const uint8_t *p, const uint8_t *end)
{
	const uint8_t *out= ff_avc_find_startcode_internal(p, end);
	if (p < out && out < end && !out[-1]) out--;
	return out;
}

const uint8_t * X264Encoder::ff_avc_find_startcode_internal(const uint8_t *p,
													 const uint8_t *end)
{
	const uint8_t *a = p + 4 - ((intptr_t)p & 3);

	for (end -= 3; p < a && p < end; p++) {
		if (p[0] == 0 && p[1] == 0 && p[2] == 1)
			return p;
	}

	for (end -= 3; p < end; p += 4) {
		uint32_t x = *(const uint32_t*)p;

		if ((x - 0x01010101) & (~x) & 0x80808080) {
			if (p[1] == 0) {
				if (p[0] == 0 && p[2] == 1)
					return p;
				if (p[2] == 0 && p[3] == 1)
					return p+1;
			}

			if (p[3] == 0) {
				if (p[2] == 0 && p[4] == 1)
					return p+2;
				if (p[4] == 0 && p[5] == 1)
					return p+3;
			}
		}
	}

	for (end += 3; p < end; p++) {
		if (p[0] == 0 && p[1] == 0 && p[2] == 1)
			return p;
	}

	return end + 3;
}

bool  X264Encoder::has_start_code(const uint8_t *data)
{
	if (data[0] != 0 || data[1] != 0)
		return false;

	return data[2] == 1 || (data[2] == 0 && data[3] == 1);
}

encoder_packet X264Encoder::parse_avc_packet(encoder_packet &src)
{
    SerializeByte serialize_byte;
	encoder_packet avc_packet = src;

	serialize_avc_data(serialize_byte,&src.data[0], src.data.size(),
	        &avc_packet.keyframe, &avc_packet.priority);
    avc_packet.drop_priority = avc_packet.priority;
    avc_packet.data = serialize_byte.GetDataByte();
    return avc_packet;
}

void X264Encoder::serialize_avc_data(SerializeByte &s, const uint8_t *data,
							   size_t size, bool *is_keyframe, int *priority)
{
	const uint8_t *nal_start, *nal_end;
	const uint8_t *end = data+size;
	int type;

	nal_start = find_startcode(data, end);
	while (true) {
		while (nal_start < end && !*(nal_start++));

		if (nal_start == end)
			break;

		type = nal_start[0] & 0x1F;

		if (type == OBS_NAL_SLICE_IDR || type == OBS_NAL_SLICE) {
			if (is_keyframe)
				*is_keyframe = (type == OBS_NAL_SLICE_IDR);
			if (priority)
				*priority = nal_start[0] >> 5;
		}

		nal_end = find_startcode(nal_start, end);
        s.write_uint32((uint32_t)(nal_end - nal_start));
        s.write(nal_start, nal_end - nal_start);
		nal_start = nal_end;
	}
}


