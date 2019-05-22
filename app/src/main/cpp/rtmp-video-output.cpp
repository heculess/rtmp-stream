
#include "rtmp-video-output.h"
#include "rtmp-defs.h"
#include "util/dstr.h"
#include "util/threading.h"

VideoOutput::VideoOutput(video_output_info &video_info):
frame_time(0)
{
	info 		= video_info;
	frame_time  = (uint64_t)(1000000000.0 * (double)video_info.fps_den /
								 (double)video_info.fps_num);
}

VideoOutput::~VideoOutput()
{
}

void VideoOutput::on_media_thread_create()
{
	os_set_thread_name("video-io: video thread");
}

void VideoOutput::on_input_mutex(media_data &frame)
{
	if (scale_video_output(input, frame))
		input.callback(input.param, &frame);

	input.last_output_timestamp = frame.timestamp;
}

bool VideoOutput::scale_video_output(video_input &input, media_data &data)
{
	if(input.last_output_timestamp >= data.timestamp)
		return  false;
	return true;
}

void VideoOutput::UpdateCache(media_data &input_frame)
{
	if(input.callback == NULL)
		return;

    update_input_frame(input_frame);
}

double VideoOutput::get_frame_rate()
{
	return (double)info.fps_num / (double)info.fps_den;
}

uint32_t VideoOutput::get_height()
{
	return info.height;
}

uint32_t VideoOutput::get_width()
{
	return info.width;
}

void VideoOutput::start_raw_video(const struct video_scale_info *conversion,
					 void (*callback)(void *param, struct media_data *frame),
					 void *param)
{
	video_output_connect(conversion, callback, param);
}

bool VideoOutput::video_output_connect(const struct video_scale_info *conversion,
						  void (*callback)(void *param, struct media_data *frame),
						  void *param)
{
	bool success = false;

	if (!callback)
		return false;

	pthread_mutex_lock(&input_mutex);

	if (!get_input_valid(callback, param)) {
		video_input vInput;

		vInput.callback = callback;
		vInput.param    = param;

		if (conversion) {
			vInput.conversion = *conversion;
		} else {
			vInput.conversion.format    = info.format;
			vInput.conversion.width     = info.width;
			vInput.conversion.height    = info.height;
		}

		if (vInput.conversion.width == 0)
			vInput.conversion.width = info.width;
		if (vInput.conversion.height == 0)
			vInput.conversion.height = info.height;

		success = input_init(vInput);
		if (success)
			input = vInput;
	}

	pthread_mutex_unlock(&input_mutex);

	return success;
}

video_output_info * VideoOutput::get_info()
{
	return &info;
}

void VideoOutput::stop_raw_video(void (*callback)(void *param, struct media_data *frame),
					void *param)
{
}

bool VideoOutput::get_input_valid(void (*callback)(void *param, struct media_data *frame),
						   void *param)
{
	if (input.callback == callback && input.param == param)
		return true;

	return false;
}

bool VideoOutput::input_init(video_input &input)
{
	return true;
}



