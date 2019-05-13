#pragma once

#include "util/platform.h"

#include "util/threading.h"
#include <inttypes.h>
#include "librtmp/rtmp.h"
#include "rtmp-defs.h"
#include <string>

#include "rtmp-circle-buffer.h"
#include "rtmp-output-base.h"
#include "rtmp-struct.h"

class RtmpStream : public rtmp_output_base
{
public:
	RtmpStream();
	virtual ~RtmpStream();

	bool start();
	void stop(uint64_t ts);
	void encoded_packet(encoder_packet &packet);
	uint64_t get_total_bytes();
	float get_congestion();
	int get_connect_time_ms();
	int get_dropped_frames();

	bool stopping();
	bool isConnecting();
	bool isDisconnected();

	std::string		  path;
	std::string		  key;
	std::string		  username;
	std::string		  password;

protected:

	pthread_mutex_t  packets_mutex;
	circlebuffer 	 packets;
	bool             sent_headers;

	bool             got_first_video;
	int64_t          start_dts_offset;

	volatile bool    connecting;
	pthread_t        connect_thread;

	volatile bool    disconnected;
	pthread_t        send_thread;

	int              max_shutdown_time_sec;

	os_sem_t         *send_sem;
	os_event_t       *stop_event;
	uint64_t         stop_ts;
	uint64_t         shutdown_timeout_ts;


	std::string		  encoder_name;
	std::string		  bind_ip;

	int64_t          drop_threshold_usec;
	int64_t          pframe_drop_threshold_usec;
	int              min_priority;
	float            congestion;

	int64_t          last_dts_usec;

	uint64_t         total_bytes_sent;
	int              dropped_frames;

	RTMP             rtmp;

	os_event_t       *buffer_space_available_event;
	os_event_t       *buffer_has_data_event;
	os_event_t       *socket_available_event;
	os_event_t       *send_thread_signaled_exit;


protected:
	size_t num_buffered_packets();

	bool init_connect();
	int try_connect();
	void set_output_error();
	bool add_video_packet(encoder_packet &packet);
	bool add_packet(encoder_packet &packet);
	void check_to_drop_frames(bool pframes);
	bool find_first_video_packet(encoder_packet &irst);
	void drop_frames(const char *name, int highest_priority, bool pframes);
	int init_send();
	bool reset_semaphore();
	bool send_meta_data();
	bool get_next_packet(encoder_packet_info &packet);
	bool can_shutdown_stream(encoder_packet &packet);
	bool send_headers();
	bool send_audio_header();
	bool send_video_header();
	int send_packet(encoder_packet &packet, bool is_header, size_t idx);
	bool discard_recv_data(size_t size);

	 void stream_destroy();

private:
	static void * connect_thread_fun(void *data);
	static void * send_thread_fun(void *data);

	void free_packets();
	bool is_stream_active();

	void set_rtmp_str(AVal *val, const char *str);

	volatile bool    stream_active;
};

