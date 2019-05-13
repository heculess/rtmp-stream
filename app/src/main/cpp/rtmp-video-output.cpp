
#include "rtmp-video-output.h"
#include "rtmp-defs.h"
#include "util/dstr.h"
#include "util/threading.h"

VideoOutput::VideoOutput(video_output_info &video_info):
frame_time(0)
{
	pthread_mutexattr_t attr;
	info 		= video_info;
	frame_time  = (uint64_t)(1000000000.0 * (double)video_info.fps_den /
								 (double)video_info.fps_num);
	initialized = false;

	do{
		if (pthread_mutexattr_init(&attr) != 0)
			break;
		if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0)
			break;
		if (pthread_mutex_init(&data_mutex, &attr) != 0)
			break;
		if (pthread_mutex_init(&input_mutex, &attr) != 0)
			break;
		if (os_sem_init(&update_semaphore, 0) != 0)
			break;
		if (pthread_create(&thread, NULL, video_thread, this) != 0)
			break;

		init_cache();

		initialized = true;

		return;

	}while(false);

	video_output_close();
}

void *VideoOutput::video_thread(void *param)
{
	VideoOutput *video = (VideoOutput *)param;

	os_set_thread_name("video-io: video thread");

	while (os_sem_wait(video->update_semaphore) == 0) {
		if (video->stop)
			break;

		while (!video->stop && !video->video_output_cur_frame()) {
            os_atomic_inc_long(&video->total_frames);
        }
	}

	return NULL;
}

void VideoOutput::init_cache()
{
}

void VideoOutput::video_output_close()
{
	video_output_stop();
	video_input_free(&input);

	os_sem_destroy(update_semaphore);
	pthread_mutex_destroy(&data_mutex);
	pthread_mutex_destroy(&input_mutex);
}

bool VideoOutput::video_output_cur_frame()
{
	struct video_data frame;
	bool complete;
	bool skipped;

	pthread_mutex_lock(&data_mutex);

	frame = cache;

	pthread_mutex_unlock(&data_mutex);

	pthread_mutex_lock(&input_mutex);

	if (scale_video_output(&input, &frame))
		input.callback(input.param, &frame);

	input.last_output_timestamp = frame.timestamp;

	pthread_mutex_unlock(&input_mutex);


	return complete;
}

void VideoOutput::video_output_stop()
{
	void *thread_ret;

	if (initialized) {
		initialized = false;
		stop = true;
		os_sem_post(update_semaphore);
		pthread_join(thread, &thread_ret);
	}
}

void VideoOutput::video_input_free(struct video_input *input)
{
	//for (size_t i = 0; i < MAX_CONVERT_BUFFERS; i++)
	//	video_frame_free(&input->frame[i]);
}

bool VideoOutput::scale_video_output(struct video_input *input,
						struct video_data *data)
{
	if(input->last_output_timestamp >= data->timestamp)
		return  false;
	return true;
}

void VideoOutput::UpdateCache(video_data *input_frame)
{
	if(input.callback == NULL)
		return;

	pthread_mutex_lock(&data_mutex);
	cache = *input_frame;
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
		struct video_input vInput;

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

		success = input_init(&vInput);
		if (success) {
			reset_frames();
			input = vInput;
		}
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
	disconnect(callback, param);
}

void VideoOutput::disconnect(void (*callback)(void *param, struct video_data *frame),
							 void *param)
{
	if (!callback)
		return;

	pthread_mutex_lock(&input_mutex);

	if (get_input_valid(callback, param)) {
		video_input_free(&input);
	}

	pthread_mutex_unlock(&input_mutex);
}

bool VideoOutput::get_input_valid(void (*callback)(void *param, struct video_data *frame),
						   void *param)
{
	if (input.callback == callback && input.param == param)
		return true;

	return false;
}

bool VideoOutput::input_init(struct video_input *input)
{
	return true;
}

void VideoOutput::reset_frames()
{
	os_atomic_set_long(&total_frames, 0);
}

uint32_t VideoOutput::get_total_frames()
{
	return (uint32_t)os_atomic_load_long(&total_frames);
}



