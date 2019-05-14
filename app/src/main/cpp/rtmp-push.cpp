
#include <string>

#include "util/threading.h"
#include "util/bmem.h"


# include "rtmp-push.h"
# include "rtmp-encoder.h"
# include "rtmp-output.h"
# include "rtmp-stream.h"
#include "util/dstr.h"
#include "rtmp-circle-buffer.h"

struct ts_info {
    uint64_t start;
    uint64_t end;
};

static inline size_t convert_time_to_frames(size_t sample_rate, uint64_t t)
{
    return (size_t)(t * (uint64_t)sample_rate / 1000000000ULL);
}

static inline void get_buffer_audio(uint64_t &buffer_ts,float *buffer)
{
    buffer_ts = os_gettime_ns();
}

static inline void get_audio_buffer(struct audio_output_data *mixe,
        size_t sample_rate, struct ts_info *ts)
{
    uint64_t audio_ts = 0;

    std::vector<float> buffer(AUDIO_OUTPUT_FRAMES,0);
    get_buffer_audio(audio_ts,&buffer[0]);

    size_t total_floats = AUDIO_OUTPUT_FRAMES;
    size_t start_point = 0;
    if (audio_ts != ts->start) {
        start_point = convert_time_to_frames(sample_rate, audio_ts - ts->start);
        if (start_point >= AUDIO_OUTPUT_FRAMES)
            return;

        total_floats -= start_point;
    }
    memcpy(&mixe->data[start_point],&buffer[0],total_floats);
}

bool audio_callback(void * param ,
                    uint64_t start_ts_in, uint64_t end_ts_in, uint64_t *out_ts,
                    uint32_t sample_rate, struct audio_output_data *mixes)
{
    ts_info ts = {start_ts_in, end_ts_in};

    circlebuffer *cb = (circlebuffer *)param;
    cb->push_back(&ts, sizeof(ts));
    cb->peek_front(&ts, sizeof(ts));

    get_audio_buffer(mixes, sample_rate,&ts);

    cb->pop_front( NULL, sizeof(ts));
    *out_ts = ts.start;
    return true;
}

void RtmpPush::audio_output_open()
{
    if(!audio)
        audio = std::make_shared<AudioOutput>(audio_info);
}

void RtmpPush::video_output_open()
{
    if(!video)
        video = std::make_shared<VideoOutput>(video_info);
}

void RtmpPush::SetupOutputs()
{
    std::dynamic_pointer_cast<X264Encoder>(h264Streaming)->set_video(video);
    std::dynamic_pointer_cast<aacEncoder>(aacStreaming)->set_audio(audio);
}

bool RtmpPush::StartStreaming(const char *stream_url, const char *stream_name)
{
    audio_output_open();
    video_output_open();

    h264Streaming = std::make_shared<X264Encoder>();
    aacStreaming = std::make_shared<aacEncoder>();

	if (!Active())
		SetupOutputs();

	if (!streamOutput) {
		streamOutput = std::make_shared<RtmpOutput>(video,audio);
        if (!streamOutput)
            return false;
	}

    std::shared_ptr<RtmpOutput> output_stream =
            std::dynamic_pointer_cast<RtmpOutput>(streamOutput);
	if(output_stream){
        output_stream->set_video_encoder(h264Streaming);
        output_stream->set_audio_encoder(aacStreaming);

        //output_stream->reconnect_retry_max = 20;
        //output_stream->reconnect_retry_sec = 10;
        output_stream->path = streamUrl;
        output_stream->key = streamName;

        if (output_stream->output_start())
            return true;
	}

	return false;
}

void RtmpPush::StopStreaming()
{
    std::shared_ptr<RtmpOutput> output_stream =
            std::dynamic_pointer_cast<RtmpOutput>(streamOutput);
    if(!output_stream)
        return;

	if (output_stream->output_active())
        output_stream->output_stop();
}

RtmpPush::RtmpPush()
{
    video_time = os_gettime_ns();

    audio_info.name = "audio";
    audio_info.samples_per_sec = 44100;
    audio_info.format = AUDIO_FORMAT_FLOAT_PLANAR;
    audio_info.speakers = SPEAKERS_MONO;
    audio_info.input_callback = audio_callback;

    video_info.fps_num = 30;
    video_info.fps_den = 1;
    video_info.width = 0;
    video_info.height = 0;

    video_info.name = "video";
    video_info.format = VIDEO_FORMAT_NONE;
    video_info.colorspace = VIDEO_CS_601;
    video_info.range = VIDEO_RANGE_PARTIAL;

}

void RtmpPush::Push_video_data(struct video_data *input_frame)
{
    std::shared_ptr<VideoOutput> video_output =
            std::dynamic_pointer_cast<VideoOutput>(video);
    if(!video_output)
        return;

    uint64_t interval_ns = video_output->frame_time;

    uint64_t cur_time = video_time;
    uint64_t t = cur_time + interval_ns;

    if (os_sleepto_ns(t)) {
        video_time = t;
    } else {
        int count = (int)((os_gettime_ns() - cur_time) / interval_ns);
        video_time = cur_time + interval_ns * count;
    }

    video_output->UpdateCache(input_frame);
}


