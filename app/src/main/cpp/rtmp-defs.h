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

#include "callback/proc.h"
#include "callback/signal.h"
#include "util/darray.h"
#include "util/circlebuf.h"
#include "util/threading.h"
#include "rtmp-struct.h"
#include <memory>
#include <android/log.h>

#define MAJOR_VER  1
#define MINOR_VER  0
#define PATCH_VER  1

#define MICROSECOND_DEN     1000000
#define MILLISECOND_DEN     1000
#define MAX_AV_PLANES       8
#define AUDIO_OUTPUT_FRAMES 1024

#define TAG "JNITEST"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,TAG,__VA_ARGS__)

#define VIDEO_OUTPUT_SUCCESS       0
#define VIDEO_OUTPUT_INVALIDPARAM -1
#define VIDEO_OUTPUT_FAIL         -2


#define CAPTION_LINE_CHARS (32)
#define CAPTION_LINE_BYTES (4*CAPTION_LINE_CHARS)

enum {
    OBS_NAL_PRIORITY_DISPOSABLE = 0,
    OBS_NAL_PRIORITY_LOW        = 1,
    OBS_NAL_PRIORITY_HIGH       = 2,
    OBS_NAL_PRIORITY_HIGHEST    = 3,
};

enum {
    OBS_NAL_UNKNOWN   = 0,
    OBS_NAL_SLICE     = 1,
    OBS_NAL_SLICE_DPA = 2,
    OBS_NAL_SLICE_DPB = 3,
    OBS_NAL_SLICE_DPC = 4,
    OBS_NAL_SLICE_IDR = 5,
    OBS_NAL_SEI       = 6,
    OBS_NAL_SPS       = 7,
    OBS_NAL_PPS       = 8,
    OBS_NAL_AUD       = 9,
    OBS_NAL_FILLER    = 12,
};

bool audio_callback(void *param,
                    uint64_t start_ts_in, uint64_t end_ts_in, uint64_t *out_ts,
                    uint32_t mixers, struct audio_output_data *mixes);


#define OBS_OUTPUT_SUCCESS         0
#define OBS_OUTPUT_BAD_PATH       -1
#define OBS_OUTPUT_CONNECT_FAILED -2
#define OBS_OUTPUT_INVALID_STREAM -3
#define OBS_OUTPUT_ERROR          -4
#define OBS_OUTPUT_DISCONNECTED   -5
