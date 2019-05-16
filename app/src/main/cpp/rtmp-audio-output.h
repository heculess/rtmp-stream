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

#include "rtmp-struct.h"
#include "rtmp-circle-buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

class AudioOutput : public media_output{
public:
	AudioOutput(audio_output_info &audio_info);
	virtual ~AudioOutput();
	uint32_t get_sample_rate();

	audio_output_info   	   info;

	size_t                     block_size;
	size_t                     channels;

	pthread_t                  thread;
	os_event_t                 *stop_event;

	audio_input_callback_t     input_cb;
	size_t                     sample_rate;
	pthread_mutex_t            input_mutex;
	audio_mix           		mixe;
	circlebuffer           		buffered_timestamps;

	bool connect(const struct audio_convert_info *conversion,
			audio_output_callback_t callback, void *param);
	void disconnect(audio_output_callback_t callback, void *param);

	const audio_output_info* get_info();

	bool output_open() override;
	void output_close() override;

private:
    static void *audio_thread(void *param);
    size_t get_audio_bytes_per_channel(enum audio_format format);
    uint64_t audio_frames_to_ns(size_t sample_rate,
                                             uint64_t frames);
    void input_and_output(uint64_t audio_time, uint64_t prev_time);
    void clamp_audio_output(size_t bytes);
    void do_audio_output(uint64_t timestamp, uint32_t frames);

	size_t audio_get_input_idx(audio_output_callback_t callback, void *param);
};

#ifdef __cplusplus
}
#endif
