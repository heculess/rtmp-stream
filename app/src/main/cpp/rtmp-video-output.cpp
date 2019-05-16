
#include "rtmp-video-output.h"
#include "rtmp-defs.h"
#include "util/dstr.h"
#include "util/threading.h"

VideoOutput::VideoOutput(video_output_info &video_info):
frame_time(0),
stop(false),
update_semaphore(NULL)
{
	info 		= video_info;
	frame_time  = (uint64_t)(1000000000.0 * (double)video_info.fps_den /
								 (double)video_info.fps_num);
	initialized = false;

    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0)
        return;
    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0)
        return;
    if (pthread_mutex_init(&data_mutex, &attr) != 0)
        return;
    if (pthread_mutex_init(&input_mutex, &attr) != 0)
        return;
    if (os_sem_init(&update_semaphore, 0) != 0)
        return;

    initialized = true;
}

VideoOutput::~VideoOutput()
{
	output_close();
    os_sem_destroy(update_semaphore);
    pthread_mutex_destroy(&data_mutex);
    pthread_mutex_destroy(&input_mutex);
}

void *VideoOutput::video_thread(void *param)
{
	VideoOutput *video = (VideoOutput *)param;

	os_set_thread_name("video-io: video thread");

	while (os_sem_wait(video->update_semaphore) == 0) {
		if (video->stop)
			break;

		while (!video->stop) {
            if(video->video_output_cur_frame())
            	break;
        }
	}

	return NULL;
}

bool VideoOutput::output_open()
{
	if (pthread_create(&thread, NULL, video_thread, this) != 0)
		return false;

	return true;
}

void VideoOutput::output_close()
{
	video_output_stop();
}

bool VideoOutput::video_output_cur_frame()
{
	video_data frame;
	pthread_mutex_lock(&data_mutex);
	frame = cache;
	pthread_mutex_unlock(&data_mutex);

	pthread_mutex_lock(&input_mutex);

	if (scale_video_output(input, frame))
		input.callback(input.param, &frame);

	input.last_output_timestamp = frame.timestamp;

	pthread_mutex_unlock(&input_mutex);

	return true;
}

void VideoOutput::video_output_stop()
{
	if (initialized) {
		stop = true;
		os_sem_post(update_semaphore);
		void *thread_ret = NULL;
		pthread_join(thread, &thread_ret);
	}
}

bool VideoOutput::scale_video_output(video_input &input, video_data &data)
{
	if(input.last_output_timestamp >= data.timestamp)
		return  false;
	return true;
}

void VideoOutput::UpdateCache(video_data &input_frame)
{
	if(input.callback == NULL)
		return;

	pthread_mutex_lock(&data_mutex);
	cache = input_frame;
	pthread_mutex_unlock(&data_mutex);

	LOGI("Push_video_data----------------------------------------------------------------------- packet size : %d",cache.data.size());

	video_output_unlock_frame();
}

void VideoOutput::video_output_unlock_frame()
{
    pthread_mutex_lock(&data_mutex);
    os_sem_post(update_semaphore);
    pthread_mutex_unlock(&data_mutex);
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
					 void (*callback)(void *param, struct video_data *frame),
					 void *param)
{
	video_output_connect(conversion, callback, param);
}

bool VideoOutput::video_output_connect(const struct video_scale_info *conversion,
						  void (*callback)(void *param, struct video_data *frame),
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

void VideoOutput::stop_raw_video(void (*callback)(void *param, struct video_data *frame),
					void *param)
{
	//disconnect(callback, param);
}

bool VideoOutput::get_input_valid(void (*callback)(void *param, struct video_data *frame),
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



