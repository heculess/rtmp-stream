#pragma once

#include <string>
#include <vector>
#include <memory>

class media_encoder;

class rtmp_output_base {
public:
	rtmp_output_base();
    virtual ~rtmp_output_base(){}

    std::string id;
	std::string encoded_video_codecs;
	std::string encoded_audio_codecs;
    uint32_t flags;
	void *type_data;

    virtual std::shared_ptr<media_encoder> get_video_encoder();
    virtual std::shared_ptr<media_encoder>get_audio_encoder();

	virtual bool can_begin_data_capture();
	virtual bool begin_data_capture();
	virtual void end_data_capture();

	virtual bool initialize_encoders();
	virtual void set_last_error(const char *message);

	virtual void signal_stop(int code);
	virtual void remove_encoder(std::shared_ptr<media_encoder> encoder);

};

