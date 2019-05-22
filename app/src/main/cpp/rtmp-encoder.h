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

#pragma once

#include "rtmp-defs.h"
#include "rtmp-struct.h"
#include "rtmp-circle-buffer.h"

/**
 * @file
 * @brief header for modules implementing encoders.
 *
 * Encoders are modules that implement some codec that can be used by libobs
 * to process output data.
 */

#define encoder_active() \
	os_atomic_load_bool(&active)
#define set_encoder_active(val) \
	os_atomic_set_bool(&active, val)

#ifdef __cplusplus
extern "C" {
#endif

class rtmp_output_base;

class media_encoder : public std::enable_shared_from_this<media_encoder>{

public:
    media_encoder();
    virtual ~media_encoder();

    std::string  id;
    enum obs_encoder_type type;
    std::string codec;

    int64_t cur_pts;
    uint32_t timebase_num;
    uint32_t timebase_den;
    uint64_t first_raw_ts;
    uint64_t start_ts;
    int64_t  offset_usec;
    uint32_t caps;

    volatile bool active;
    bool initialized;
    bool first_received;
    bool destroy_on_stop;
    std::weak_ptr<media_encoder> paired_encoder;

    pthread_mutex_t outputs_mutex;
    std::weak_ptr<rtmp_output_base> output;

    pthread_mutex_t init_mutex;

    std::weak_ptr<media_output>   media;

    pthread_mutex_t                 callbacks_mutex;
    std::vector<encoder_callback>   callbacks;

    virtual std::string get_name(){return "";};

    bool initialize();

    virtual bool encode(encoder_frame &frame,
                        encoder_packet &packet, bool &received_packet) {return false;};
    virtual bool get_extra_data(std::vector<uint8_t> &data) {return false;};
	virtual std::vector<uint8_t> get_encode_header() {return std::vector<uint8_t>();};

    void set_output(std::shared_ptr<rtmp_output_base> op);
    void remove_output();

    void do_encode(encoder_frame &frame);

    void start(void (*new_packet)(void *param, encoder_packet &packet),
               void *param);

    void stop(void (*new_packet)(void *param, encoder_packet &packet),
              void *param);

private:

    bool initialize_internal();
    virtual  void on_initialize_internal(){}

    void full_stop();
    void shutdown();

    void send_off_encoder_packet(bool success, bool received, encoder_packet &pkt);
    void send_packet(struct encoder_callback *cb, encoder_packet &packet);
    void send_first_video_packet(struct encoder_callback *cb,
                                 encoder_packet &packet);

    void start_internal(void (*new_packet)(void *param, encoder_packet &packet),
                        void *param);
    bool stop_internal(void (*new_packet)(void *param, encoder_packet &packet),
                       void *param);

    size_t get_callback_idx(
            void (*new_packet)(void *param, encoder_packet &packet),void *param);

    virtual  void add_connection(){}
    void remove_connection();
    virtual  void on_remove_connection(){}

    void actually_destroy();
    virtual  void on_actually_destroy(){}

};

class SerializeByte;

class X264Encoder : public media_encoder
{
public:
	X264Encoder();
	virtual ~X264Encoder();

	void set_video(std::shared_ptr<media_output> &video);
	void set_scaled_size(uint32_t width, uint32_t height);

	std::string get_name() override;
	bool encode(encoder_frame &frame,
				encoder_packet &packet, bool &received_packet) override;
	bool get_extra_data(std::vector<uint8_t> &extra_data) override;
	std::vector<uint8_t> get_encode_header() override;
	bool get_sei_data(std::vector<uint8_t> &sei_data);
    static encoder_packet parse_avc_packet(encoder_packet &src);

	uint32_t get_width();
	uint32_t get_height();

	static std::vector<uint8_t> parse_header(const std::vector<uint8_t> &data);

private:
	void get_video_info(video_scale_info &info);
	void load_headers();
	bool valid_format(video_format format);
	void get_info(video_scale_info &info);
	void clear_data();
	void add_connection() override ;
	void on_remove_connection() override ;
	void on_initialize_internal() override ;
	enum video_format get_preferred_video_format();


    static void get_sps_pps(const uint8_t *data, size_t size,
								  const uint8_t **sps, size_t *sps_size,
								  const uint8_t **pps, size_t *pps_size);
    static const uint8_t * find_startcode(const uint8_t *p, const uint8_t *end);
    static const uint8_t * ff_avc_find_startcode_internal(const uint8_t *p, const uint8_t *end);
    static bool  has_start_code(const uint8_t *data);

    static void serialize_avc_data(SerializeByte &s, const uint8_t *data,
										 size_t size, bool *is_keyframe, int *priority);


	std::vector<uint8_t> extra_data;
	std::vector<uint8_t> sei;

	uint32_t scaled_width;
	uint32_t scaled_height;
	enum video_format preferred_format;
};

class aacEncoder : public media_encoder {
public:
	aacEncoder();
	virtual ~aacEncoder();
	void set_audio(std::shared_ptr<media_output> &audio);

	std::string get_name() override;
	bool encode(encoder_frame &frame,
				encoder_packet &packet, bool &received_packet) override;

	bool get_extra_data(std::vector<uint8_t> &data) override;
	std::vector<uint8_t> get_encode_header() override;

	void clear_audio();
	bool buffer_audio(media_data &data);
	void send_audio_data();

	uint32_t get_sample_rate();

    circlebuffer audio_input_buffer;
	std::vector<uint8_t> audio_output_buffer;

private:
	void get_audio_info(audio_convert_info &info);

	void on_actually_destroy() override;
	void add_connection() override ;
	void on_remove_connection() override ;

	void free_audio_buffers();
	void intitialize_audio_encoder();
	void reset_audio_buffers();

	void push_back_audio(media_data &data, size_t size, size_t offset_size);
	size_t calc_offset_size(uint64_t v_start_ts, uint64_t a_start_ts);
	void start_from_buffer(uint64_t v_start_ts);

	void on_initialize_internal() override ;

	void load_headers();

	std::string type;
	int64_t total_samples;

	size_t audio_planes;
	size_t audio_size;

	int frame_size_bytes;

	uint32_t samplerate;

	size_t blocksize;
	size_t framesize;

	std::vector<uint8_t> extra_data;
};

#ifdef __cplusplus
}
#endif
