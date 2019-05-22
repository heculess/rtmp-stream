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

    void UpdateCache(media_data &input_frame);

	audio_output_info   	   info;

	size_t                     block_size;
	size_t                     channels;

	size_t                     sample_rate;
	audio_mix           		mixe;

	std::vector<uint8_t>        format;

	bool connect(const struct audio_convert_info *conversion,
			audio_output_callback_t callback, void *param);
	void disconnect(audio_output_callback_t callback, void *param);

	const audio_output_info* get_info();

protected:
	void on_media_thread_create() override ;
	void on_input_mutex(media_data &frame) override ;

private:
    size_t get_audio_bytes_per_channel(enum audio_format format);
	size_t audio_get_input_idx(audio_output_callback_t callback, void *param);
};

#ifdef __cplusplus
}
#endif
