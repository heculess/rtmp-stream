#include "rtmp-output-base.h"
# include "rtmp-encoder.h"


rtmp_output_base::rtmp_output_base():
flags(0),
type_data(NULL)
{
}

std::shared_ptr<media_encoder> rtmp_output_base::get_video_encoder()
{
	return std::shared_ptr<media_encoder>();
}

std::shared_ptr<media_encoder> rtmp_output_base::get_audio_encoder()
{
	return std::shared_ptr<media_encoder>();
}

bool rtmp_output_base::can_begin_data_capture()
{
	return false;
}

bool rtmp_output_base::begin_data_capture()
{
	return false;
}

void rtmp_output_base::end_data_capture()
{
}

bool rtmp_output_base::initialize_encoders()
{
	return  false;
}

void rtmp_output_base::set_last_error(const char *message)
{
}

void rtmp_output_base::signal_stop(int code)
{
}

void rtmp_output_base::remove_encoder(std::shared_ptr<media_encoder> encoder)
{
}