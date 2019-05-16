# include "rtmp-audio-output.h"
#include "util/platform.h"
#include "util/darray.h"
#include "util/dstr.h"
#include "util/util_uint128.h"
#include "rtmp-defs.h"

AudioOutput::AudioOutput(audio_output_info &audio_info):
block_size(0),
channels(0),
sample_rate(0),
stop_event(NULL)
{
	info         = audio_info;
	channels     = audio_info.speakers;
	input_cb     = audio_info.input_callback;
	sample_rate  = audio_info.samples_per_sec;
	block_size   = get_audio_bytes_per_channel(audio_info.format);

    initialized = false;
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0)
        return;
    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0)
        return;
    if (pthread_mutex_init(&input_mutex, &attr) != 0)
        return;
    if (os_event_init(&stop_event, OS_EVENT_TYPE_MANUAL) != 0)
        return;

    initialized  = true;
}

AudioOutput::~AudioOutput()
{
    output_close();
    mixe.inputs.clear();
    os_event_destroy(stop_event);
    pthread_mutex_destroy(&input_mutex);
}

bool AudioOutput::output_open()
{
    os_event_reset(stop_event);

    if (pthread_create(&thread, NULL, audio_thread, this) != 0)
        return false;

    return true;
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

void *AudioOutput::audio_thread(void *param)
{
    AudioOutput *audio = (AudioOutput *)param;

    uint64_t rate = audio->info.samples_per_sec;
    uint64_t samples = 0;
    uint64_t start_time = os_gettime_ns();
    uint64_t prev_time = start_time;
    uint64_t audio_time = prev_time;
    uint32_t audio_wait_time =
            (uint32_t)(audio->audio_frames_to_ns(rate, AUDIO_OUTPUT_FRAMES) /
                       1000000);

    os_set_thread_name("audio-io: audio thread");

    while (os_event_try(audio->stop_event) == EAGAIN) {
        uint64_t cur_time;
        os_sleep_ms(audio_wait_time);

        cur_time = os_gettime_ns();
        while (audio_time <= cur_time) {
            samples += AUDIO_OUTPUT_FRAMES;
            audio_time = start_time +
                    audio->audio_frames_to_ns(rate, samples);

            audio->input_and_output(audio_time, prev_time);
            prev_time = audio_time;
        }
    }

    return NULL;
}

void AudioOutput::output_close()
{
    if (initialized) {
        os_event_signal(stop_event);
        void *thread_ret = NULL;
        pthread_join(thread, &thread_ret);
    }
}

uint64_t AudioOutput::audio_frames_to_ns(size_t sample_rate,
                            uint64_t frames)
{
    util_uint128_t val;
    val = util_mul64_64(frames, 1000000000ULL);
    val = util_div128_32(val, (uint32_t)sample_rate);
    return val.low;
}

void AudioOutput::input_and_output(uint64_t audio_time, uint64_t prev_time)
{
    size_t bytes = AUDIO_OUTPUT_FRAMES * block_size;
    audio_output_data data;
    uint64_t new_ts = 0;
    // clear mix buffers
    memset(mixe.buffer, 0, AUDIO_OUTPUT_FRAMES * sizeof(float));

    // get new audio data
    if (!input_cb(&buffered_timestamps, prev_time, audio_time,
                  &new_ts, sample_rate, &data))
        return;

    clamp_audio_output(bytes);
    do_audio_output(new_ts, AUDIO_OUTPUT_FRAMES);

}

void AudioOutput::clamp_audio_output(size_t bytes)
{
    size_t float_size = bytes / sizeof(float);

    if (!mixe.inputs.size())
        return;

    float *mix_data = mixe.buffer;
    float *mix_end = &mix_data[float_size];

    while (mix_data < mix_end) {
        float val = *mix_data;
        val = (val >  1.0f) ?  1.0f : val;
        val = (val < -1.0f) ? -1.0f : val;
        *(mix_data++) = val;
    }
}

void AudioOutput::do_audio_output(uint64_t timestamp, uint32_t frames)
{
    audio_data data;

    pthread_mutex_lock(&input_mutex);

    for (size_t i = mixe.inputs.size(); i > 0; i--) {

        int buffer_szie = frames*sizeof(float);
        data.data.resize(buffer_szie,0);
        memcpy(&data.data[0],mixe.buffer,buffer_szie);
        data.timestamp = timestamp;

        mixe.inputs[i-1].callback(mixe.inputs[i-1].param, &data);
    }

    pthread_mutex_unlock(&input_mutex);
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


