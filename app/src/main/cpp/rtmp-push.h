

#pragma once

#include "rtmp-defs.h"
#include "rtmp-encoder.h"
#include "rtmp-output.h"
#include "callback/signal.h"

# include "rtmp-audio-output.h"
# include "rtmp-video-output.h"


class RtmpPush {
public:
    std::shared_ptr<rtmp_output_base> streamOutput;

    std::shared_ptr<media_encoder> aacStreaming;
    std::shared_ptr<media_encoder> h264Streaming;

    std::string            streamUrl;
    std::string            streamName;

    std::shared_ptr<media_output> video;
    std::shared_ptr<media_output> audio;

    uint64_t                video_time;

    video_output_info       video_info;
    audio_output_info       audio_info;

    bool streamingActive = false;

    RtmpPush();

    void SetupOutputs();
    int GetAudioBitrate();


    bool StartStreaming(const char *stream_url, const char *stream_name);
    void StopStreaming();

    void Push_video_data(struct video_data *input_frame);

    inline bool Active()
    {
        return streamingActive;
    }

private:
    void video_output_open();
    void audio_output_open();

};

