
#include <string>

#include "util/threading.h"
#include "util/bmem.h"


# include "rtmp-push.h"
# include "rtmp-encoder.h"
# include "rtmp-output.h"
# include "rtmp-stream.h"
#include "util/dstr.h"
#include "rtmp-circle-buffer.h"

void RtmpPush::audio_output_open()
{
    if(!audio)
        audio = std::make_shared<AudioOutput>(audio_info);

    audio->output_open();
}

void RtmpPush::video_output_open()
{
    if(!video)
        video = std::make_shared<VideoOutput>(video_info);

    video->output_open();
}

void RtmpPush::SetupOutputs()
{
    std::dynamic_pointer_cast<X264Encoder>(h264Streaming)->set_video(video);
    //std::dynamic_pointer_cast<aacEncoder>(aacStreaming)->set_audio(audio);
}

bool RtmpPush::StartStreaming(const char *stream_url, const char *stream_name)
{
    //audio_output_open();
    video_output_open();

    if(!h264Streaming)
        h264Streaming = std::make_shared<X264Encoder>();
    /*
    if(!aacStreaming)
        aacStreaming = std::make_shared<aacEncoder>();
*/
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
        //output_stream->set_audio_encoder(aacStreaming);

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
    output_stream->output_stop();
}

RtmpPush::RtmpPush()
{
    audio_info.name = "audio";
    audio_info.samples_per_sec = 44100;
    audio_info.format = AUDIO_FORMAT_FLOAT_PLANAR;
    audio_info.speakers = SPEAKERS_MONO;

    video_info.fps_num = 30;
    video_info.fps_den = 1;
    video_info.width = 0;
    video_info.height = 0;

    video_info.name = "video";
    video_info.format = VIDEO_FORMAT_NONE;
    video_info.colorspace = VIDEO_CS_601;
    video_info.range = VIDEO_RANGE_PARTIAL;

}

void RtmpPush::Push_video_data(media_data &input_frame)
{
    std::shared_ptr<VideoOutput> video_output =
            std::dynamic_pointer_cast<VideoOutput>(video);
    if(!video_output)
        return;
    video_output->UpdateCache(input_frame);
}

void RtmpPush::Push_audio_data(media_data &input_frame)
{
    std::shared_ptr<AudioOutput> audio_output =
            std::dynamic_pointer_cast<AudioOutput>(audio);
    if(!audio_output)
        return;

    audio_output->UpdateCache(input_frame);
}


