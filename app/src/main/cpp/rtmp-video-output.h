
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

	double get_frame_rate();
	uint32_t get_height();
	uint32_t get_width();

    void UpdateCache(media_data &input_frame);

	void start_raw_video(const struct video_scale_info *conversion,
									  void (*callback)(void *param, struct media_data *frame),
									  void *param);
	void stop_raw_video(void (*callback)(void *param, struct media_data *frame),
									 void *param);
	video_output_info * get_info();

protected:
	video_output_info            		info;

    void on_media_thread_create() override ;
    void on_input_mutex(media_data &frame) override ;

private:

	bool scale_video_output(video_input &input, media_data &data);
	bool video_output_connect(const struct video_scale_info *conversion,
										   void (*callback)(void *param, struct media_data *frame),
										   void *param);
	bool get_input_valid(void (*callback)(void *param, struct media_data *frame),
									  void *param);
	bool input_init(video_input &input);
};

#ifdef __cplusplus
}
#endif
