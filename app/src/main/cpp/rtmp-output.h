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

#include "rtmp-stream.h"
#include "rtmp-encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OBS_OUTPUT_VIDEO       (1<<0)
#define OBS_OUTPUT_AUDIO       (1<<1)
#define OBS_OUTPUT_AV          (OBS_OUTPUT_VIDEO | OBS_OUTPUT_AUDIO)
#define OBS_OUTPUT_ENCODED     (1<<2)
#define OBS_OUTPUT_MULTI_TRACK (1<<4)


class RtmpOutput : public RtmpStream, public std::enable_shared_from_this<RtmpOutput>
{
public:
	RtmpOutput(std::shared_ptr<media_output> &v, std::shared_ptr<media_output> &a);
    virtual ~RtmpOutput();

    std::shared_ptr<media_encoder> get_video_encoder() override ;
    std::shared_ptr<media_encoder>get_audio_encoder() override ;

	bool can_begin_data_capture() override;
	bool begin_data_capture() override;

	void end_data_capture() override;
	void set_last_error(const char *message) override;

	bool initialize_encoders() override;
	void signal_stop(int code) override;

	void remove_encoder(std::shared_ptr<media_encoder> encoder) override;

	bool output_start();
    bool output_active();
    void output_stop();

    void set_video_encoder(std::shared_ptr<media_encoder> &encoder);
    void set_audio_encoder(std::shared_ptr<media_encoder> &encoder);

	int                                 reconnect_retry_sec = 0;
	int                                 reconnect_retry_max = 0;
	std::weak_ptr<media_encoder>		video_encoder;
	std::weak_ptr<media_encoder>		audio_encoder;

	void on_interleave_packets(encoder_packet &packet);

private:
	bool                                owns_info_id = false;

	bool                                received_video = false;
	bool                                received_audio = false;
	volatile bool                       data_active = false;
	volatile bool                       end_data_capture_thread_active = false;
	int64_t                             video_offset = 0;
	int64_t                             audio_offset = 0;
	int64_t                             highest_audio_ts = 0;
	int64_t                             highest_video_ts = 0;
	pthread_t                           end_data_capture_thread;
	os_event_t                          *stopping_event = NULL;
	pthread_mutex_t                     interleaved_mutex;
	std::vector<encoder_packet>         interleaved_packets;
	int                                 stop_code = 0;


	int                                 reconnect_retries = 0;
	int                                 reconnect_retry_cur_sec = 0;
	pthread_t                           reconnect_thread;
	os_event_t                          *reconnect_stop_event = NULL;
	volatile bool                       reconnecting = false;
	volatile bool                       reconnect_thread_active = false;

	int                                 total_frames = 0;

	volatile bool                       active = false;
    std::weak_ptr<media_output>         video;
    std::weak_ptr<media_output>         audio;


	uint32_t                            scaled_width = 0;
	uint32_t                            scaled_height = 0;

	bool                                video_conversion_set = false;
	bool                                audio_conversion_set = false;
	video_scale_info             		video_conversion;
	audio_convert_info           		audio_conversion;

	bool                                 valid = false;

	std::string                         last_error_message;

	static void *end_data_capture_thread_fun(void *data);
	static void * reconnect_thread_fun(void *param);

	bool can_begin_data_capture(bool encoded, bool has_video,
			bool has_audio);
	void end_data_capture_internal(bool signal);

	void signal_stop();

	void convert_flags(uint32_t flags, bool *encoded, bool *has_video, bool *has_audio);
	void hook_data_capture(bool encoded, bool has_video, bool has_audio);
	void do_output_signal(const char *signal);
	bool audio_valid(bool encoded);
	void pair_encoders();
	bool can_reconnect(int code);
	void output_reconnect();
	void reset_packet_data();
	void start_audio_encoders(encoded_callback_t encoded_callback);
	void stop_audio_encoders(encoded_callback_t encoded_callback);
	struct video_scale_info *get_video_conversion();
	bool isReconnecting();
	void signal_reconnect();

	uint32_t video_get_width();
	uint32_t video_get_height();
	bool actual_start();

	bool is_data_active();

	bool has_scaling();
    void force_stop();
    void actual_stop(bool force, uint64_t ts);

    void destroy();

	bool has_higher_opposing_ts(encoder_packet &packet);
	void send_interleaved();
	void discard_unused_audio_packets(int64_t dts_usec);
	void apply_interleaved_packet_offset(encoder_packet &out);
	void discard_to_idx(size_t idx);
	void check_received(encoder_packet &out);
	void insert_interleaved_packet(encoder_packet &out);
	void set_higher_ts(encoder_packet &packet);
	bool prune_interleaved_packets();
	int prune_premature_packets();
	int find_last_packet_type_idx(enum obs_encoder_type type, size_t audio_idx);
	int find_first_packet_type_idx(obs_encoder_type type, size_t audio_idx);
	bool find_first_packet_type(encoder_packet &packet, enum obs_encoder_type type,
														size_t audio_idx);
	size_t get_interleaved_start_idx();
	bool initialize_interleaved_packets();
	bool get_audio_and_video_packets(encoder_packet &ideo, encoder_packet &audio);
	bool find_last_packet_type(encoder_packet &packet, enum obs_encoder_type type,
													   size_t audio_idx);
	void resort_interleaved_packets();
	void free_packets();

	bool is_active();
};

#ifdef __cplusplus
}
#endif
