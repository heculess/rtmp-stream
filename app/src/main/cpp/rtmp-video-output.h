
#pragma once

#include "rtmp-struct.h"
#include "util/threading.h"

#ifdef __cplusplus
extern "C" {
#endif

class VideoOutput : public media_output{
public:
	VideoOutput(video_output_info &video_info);
	virtual ~VideoOutput();
	uint64_t                            frame_time;
	video_input                  		input;
	std::vector<uint8_t>                format_csd0;
	std::vector<uint8_t>                format_csd1;

	void UpdateCache(video_data &input_frame);
	double get_frame_rate();
	uint32_t get_height();
	uint32_t get_width();

	void start_raw_video(const struct video_scale_info *conversion,
									  void (*callback)(void *param, struct video_data *frame),
									  void *param);
	void stop_raw_video(void (*callback)(void *param, struct video_data *frame),
									 void *param);
	video_output_info * get_info();

	bool output_open() override;
	void output_close() override;

protected:
	video_output_info            		info;

	pthread_t                           thread;
	pthread_mutex_t                     data_mutex;
	bool                                stop;

	os_sem_t                            *update_semaphore;
	//volatile long                       total_frames;

	pthread_mutex_t                     input_mutex;
	video_data                          cache;

private:
	static void *video_thread(void *param);

	bool video_output_cur_frame();
	void video_output_stop();
	bool scale_video_output(video_input &input, video_data &data);
    void video_output_unlock_frame();

	bool video_output_connect(const struct video_scale_info *conversion,
										   void (*callback)(void *param, struct video_data *frame),
										   void *param);
	bool get_input_valid(void (*callback)(void *param, struct video_data *frame),
									  void *param);
	bool input_init(video_input &input);
};

#ifdef __cplusplus
}
#endif
