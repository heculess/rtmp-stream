# include "rtmp-audio-output.h"
#include "util/platform.h"
#include "util/darray.h"
#include "util/dstr.h"
#include "util/util_uint128.h"
#include "rtmp-defs.h"

AudioOutput::AudioOutput(audio_output_info &audio_info):
block_size(0),
channels(0),
sample_rate(0)
{
	info         = audio_info;
	channels     = audio_info.speakers;
	sample_rate  = audio_info.samples_per_sec;
	block_size   = get_audio_bytes_per_channel(audio_info.format);
}

AudioOutput::~AudioOutput()
{
    output_close();
    mixe.inputs.clear();
}

size_t AudioOutput::get_audio_bytes_per_channel(enum audio_format format)
{
    switch (format) {
        case AUDIO_FORMAT_U8BIT:
        case AUDIO_FORMAT_U8BIT_PLANAR:
            return 1;

        case AUDIO_FORMAT_16BIT:
        case AUDIO_FORMAT_16BIT_PLANAR:
            return 2;

        case AUDIO_FORMAT_FLOAT:
        case AUDIO_FORMAT_FLOAT_PLANAR:
        case AUDIO_FORMAT_32BIT:
        case AUDIO_FORMAT_32BIT_PLANAR:
            return 4;

        case AUDIO_FORMAT_UNKNOWN:
            return 0;
    }

    return 0;
}

void AudioOutput::on_media_thread_create()
{
    os_set_thread_name("audio-io: audio thread");
}


void AudioOutput::on_input_mutex(media_data &frame)
{
    for (size_t i = mixe.inputs.size(); i > 0; i--) {
        mixe.inputs[i-1].callback(mixe.inputs[i-1].param, frame);
    }
}

uint32_t AudioOutput::get_sample_rate()
{
    return info.samples_per_sec;
}

bool AudioOutput::connect(const struct audio_convert_info *conversion,
                          audio_output_callback_t callback, void *param)
{
    bool success = false;

    pthread_mutex_lock(&input_mutex);

    if (audio_get_input_idx(callback, param) == DARRAY_INVALID) {
        audio_input aInput;
        aInput.callback = callback;
        aInput.param    = param;

        if (conversion) {
            aInput.conversion = *conversion;
        } else {
            aInput.conversion.format = info.format;
            aInput.conversion.speakers = info.speakers;
            aInput.conversion.samples_per_sec =
                    info.samples_per_sec;
        }

        if (aInput.conversion.format == AUDIO_FORMAT_UNKNOWN)
            aInput.conversion.format = info.format;
        if (aInput.conversion.speakers == SPEAKERS_UNKNOWN)
            aInput.conversion.speakers = info.speakers;
        if (aInput.conversion.samples_per_sec == 0)
            aInput.conversion.samples_per_sec =
                    info.samples_per_sec;

        mixe.inputs.push_back(aInput);
    }

    pthread_mutex_unlock(&input_mutex);

    return success;
}

const audio_output_info *AudioOutput::get_info()
{
    return &info;
}

size_t AudioOutput::audio_get_input_idx(audio_output_callback_t callback, void *param)
{
    for (size_t i = 0; i < mixe.inputs.size(); i++) {
        if (mixe.inputs[i].callback == callback && mixe.inputs[i].param == param)
            return i;
    }

    return DARRAY_INVALID;
}

void AudioOutput::disconnect(audio_output_callback_t callback, void *param)
{
    pthread_mutex_lock(&input_mutex);

    size_t idx = audio_get_input_idx(callback, param);
    if (idx != DARRAY_INVALID)
        mixe.inputs.erase(mixe.inputs.begin()+idx);

    pthread_mutex_unlock(&input_mutex);
}

void AudioOutput::UpdateCache(media_data &input_frame)
{
    if(mixe.inputs.size() == 0)
        return;

    update_input_frame(input_frame);
}


