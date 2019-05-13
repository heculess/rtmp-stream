
# include "rtmp-encoder.h"

# include "rtmp-audio-output.h"

static void receive_audio(void *param, struct audio_data *data);

static inline uint64_t convert_speaker_layout(enum speaker_layout layout) {
    switch (layout) {
        case SPEAKERS_UNKNOWN:
            return 0;
        case SPEAKERS_MONO:
            return AV_CH_LAYOUT_MONO;
        case SPEAKERS_STEREO:
            return AV_CH_LAYOUT_STEREO;
        case SPEAKERS_2POINT1:
            return AV_CH_LAYOUT_SURROUND;
        case SPEAKERS_4POINT0:
            return AV_CH_LAYOUT_4POINT0;
        case SPEAKERS_4POINT1:
            return AV_CH_LAYOUT_4POINT1;
        case SPEAKERS_5POINT1:
            return AV_CH_LAYOUT_5POINT1_BACK;
        case SPEAKERS_7POINT1:
            return AV_CH_LAYOUT_7POINT1;
    }

    /* shouldn't get here */
    return 0;
}

static inline enum speaker_layout convert_ff_channel_layout(uint64_t channel_layout) {
    switch (channel_layout) {
        case AV_CH_LAYOUT_MONO:
            return SPEAKERS_MONO;
        case AV_CH_LAYOUT_STEREO:
            return SPEAKERS_STEREO;
        case AV_CH_LAYOUT_SURROUND:
            return SPEAKERS_2POINT1;
        case AV_CH_LAYOUT_4POINT0:
            return SPEAKERS_4POINT0;
        case AV_CH_LAYOUT_4POINT1:
            return SPEAKERS_4POINT1;
        case AV_CH_LAYOUT_5POINT1_BACK:
            return SPEAKERS_5POINT1;
        case AV_CH_LAYOUT_7POINT1:
            return SPEAKERS_7POINT1;
    }

    /* shouldn't get here */
    return SPEAKERS_UNKNOWN;
}

static inline bool is_audio_planar(enum audio_format format) {
    switch (format) {
        case AUDIO_FORMAT_U8BIT:
        case AUDIO_FORMAT_16BIT:
        case AUDIO_FORMAT_32BIT:
        case AUDIO_FORMAT_FLOAT:
            return false;

        case AUDIO_FORMAT_U8BIT_PLANAR:
        case AUDIO_FORMAT_FLOAT_PLANAR:
        case AUDIO_FORMAT_16BIT_PLANAR:
        case AUDIO_FORMAT_32BIT_PLANAR:
            return true;

        case AUDIO_FORMAT_UNKNOWN:
            return false;
    }

    return false;
}

static inline uint32_t get_audio_channels(enum speaker_layout speakers) {
    switch (speakers) {
        case SPEAKERS_MONO:
            return 1;
        case SPEAKERS_STEREO:
            return 2;
        case SPEAKERS_2POINT1:
            return 3;
        case SPEAKERS_4POINT0:
            return 4;
        case SPEAKERS_4POINT1:
            return 5;
        case SPEAKERS_5POINT1:
            return 6;
        case SPEAKERS_7POINT1:
            return 8;
        case SPEAKERS_UNKNOWN:
            return 0;
    }

    return 0;
}

static inline size_t get_audio_bytes_per_channel(enum audio_format format) {
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

static inline size_t get_audio_planes(enum audio_format format,
                                      enum speaker_layout speakers) {
    return (is_audio_planar(format) ? get_audio_channels(speakers) : 1);
}

static inline size_t get_audio_size(enum audio_format format,
                                    enum speaker_layout speakers, uint32_t frames) {
    bool planar = is_audio_planar(format);

    return (planar ? 1 : get_audio_channels(speakers)) *
           get_audio_bytes_per_channel(format) *
           frames;
}

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

static inline int64_t rescale_ts(int64_t val, AVCodecContext *context,
                                 AVRational new_base) {
    return av_rescale_q_rnd(val, context->time_base, new_base, AV_ROUND_PASS_DEFAULT);
}

aacEncoder::aacEncoder():
samplerate(0),
blocksize(0),
total_samples(0),
audio_planes(0),
audio_size(0),
framesize(0),
frame_size_bytes(0),
codec(NULL),
context(NULL),
aframe(NULL)
{
    id = "ffmpeg_aac";
    type = OBS_ENCODER_AUDIO;
    samples.resize(MAX_AV_PLANES,0);
    //codec = "AAC";
}

aacEncoder::~aacEncoder()
{
}

std::string aacEncoder::get_name() {
    return "FFmpegAAC";
}

void aacEncoder::set_audio(std::shared_ptr<media_output> &audio)
{
    if (!audio)
        return;

    media        = audio;
    timebase_num = 1;
    timebase_den = std::dynamic_pointer_cast<AudioOutput>(audio)->get_sample_rate();
}

void aacEncoder::destroy() {
    if (samples[0])
        av_freep(&samples[0]);
    if (context)
        avcodec_close(context);
    if (aframe)
        av_frame_free(&aframe);
}

bool aacEncoder::encode(struct encoder_frame *frame,
            encoder_packet &packet, bool *received_packet) {

    memcpy(samples[0], (void *)&frame->data[0], frame_size_bytes);

    *received_packet = true;
    packet.data.resize(133,0);
    return encode(packet, received_packet);
}

size_t aacEncoder::get_frame_size() {
    return frame_size;
}

bool aacEncoder::get_extra_data(std::vector<uint8_t> &extra_data){
    extra_data.resize(context->extradata_size,0);
    memcpy(&extra_data[0],context->extradata,extra_data.size());
    return true;
}

std::vector<uint8_t> aacEncoder::get_encode_header()
{
    std::vector<uint8_t> header;
    get_extra_data(header);
    return header;
}

void aacEncoder::get_info(struct audio_convert_info *info) {
    info->format = convert_ffmpeg_sample_format(context->sample_fmt);
    info->samples_per_sec = (uint32_t) context->sample_rate;
    info->speakers = convert_ff_channel_layout(context->channel_layout);
}

void *aacEncoder::enc_create(const char *tp, const char *alt) {
    int bitrate = 160;
    std::shared_ptr<AudioOutput> audio =
            std::dynamic_pointer_cast<AudioOutput>(media.lock());

    codec = avcodec_find_encoder_by_name(tp);
    type = tp;

    if (!codec && alt) {
        codec = avcodec_find_encoder_by_name(alt);
        type = alt;
    }

    do{
        if (!codec)
            break;

        if (!bitrate)
            return NULL;

        context = avcodec_alloc_context3(codec);
        if (!context)
            break;

        context->bit_rate = bitrate * 1000;
        context->channels = audio->channels;
        context->channel_layout = convert_speaker_layout(audio->info.speakers);
        context->sample_rate = audio->info.samples_per_sec;
        context->sample_fmt = codec->sample_fmts ?
                                   codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;

        /* check to make sure sample rate is supported */
        if (codec->supported_samplerates) {
            const int *rate = codec->supported_samplerates;
            int cur_rate = context->sample_rate;
            int closest = 0;

            while (*rate) {
                int dist = abs(cur_rate - *rate);
                int closest_dist = abs(cur_rate - closest);

                if (dist < closest_dist)
                    closest = *rate;
                rate++;
            }

            if (closest)
                context->sample_rate = closest;
        }

        if (strcmp(codec->name, "aac") == 0) {
            av_opt_set(context->priv_data, "aac_coder", "fast", 0);
        }

        init_sizes();

        /* enable experimental FFmpeg encoder if the only one available */
        context->strict_std_compliance = -2;
        context->flags = CODEC_FLAG_GLOBAL_H;

        if (initialize_codec())
            return this;
    }
    while(false);

    destroy();
    return NULL;
}

bool aacEncoder::encode(encoder_packet &packet, bool *received_packet) {
    AVRational time_base = {1, context->sample_rate};
    AVPacket avpacket = {0};
    int got_packet;
    int ret;

    aframe->nb_samples = frame_size;
    aframe->pts = av_rescale_q(total_samples,
                                    (AVRational) {1, context->sample_rate},
                                    context->time_base);

    ret = avcodec_fill_audio_frame(aframe, context->channels,
                                   context->sample_fmt, samples[0],
                                   frame_size_bytes * context->channels, 1);
    if (ret < 0)
        return false;

    total_samples += frame_size;

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(57, 40, 101)
    ret = avcodec_send_frame(context, aframe);
    if (ret == 0)
        ret = avcodec_receive_packet(context, &avpacket);

    got_packet = (ret == 0);

    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
        ret = 0;
#else
    ret = avcodec_encode_audio2(enc->context, &avpacket, enc->aframe,
            &got_packet);
#endif
    if (ret < 0)
        return false;

    *received_packet = !!got_packet;
    if (!got_packet)
        return true;

    packet.pts = rescale_ts(avpacket.pts, context, time_base);
    packet.dts = rescale_ts(avpacket.dts, context, time_base);
    packet.data.resize(avpacket.size,0);
    if(avpacket.size > 0)
        memcpy(&packet.data[0],avpacket.data,packet.data.size());
    packet.type = OBS_ENCODER_AUDIO;
    packet.timebase_num = 1;
    packet.timebase_den = (int32_t) context->sample_rate;
    av_free_packet(&avpacket);
    return true;
}

uint32_t aacEncoder::get_sample_rate()
{
    if (media.expired())
        return 0;

    std::shared_ptr<AudioOutput> audio =
            std::dynamic_pointer_cast<AudioOutput>(media.lock());
    if(!audio)
        return 0;

    return samplerate != 0 ?
           samplerate :audio->get_sample_rate();
}

void aacEncoder::on_actually_destroy()
{
    free_audio_buffers();
}

void aacEncoder::add_connection()
{
    struct audio_convert_info audio_info = {0};
    get_audio_info(&audio_info);

    std::shared_ptr<AudioOutput> audio =
            std::dynamic_pointer_cast<AudioOutput>(media.lock());
    audio->connect(&audio_info, receive_audio, this);
}

void aacEncoder::on_remove_connection()
{
    std::shared_ptr<AudioOutput> audio =
            std::dynamic_pointer_cast<AudioOutput>(media.lock());
    audio->disconnect(receive_audio, this);
}

void aacEncoder::get_audio_info(audio_convert_info *info)
{
    std::shared_ptr<AudioOutput> audio =
            std::dynamic_pointer_cast<AudioOutput>(media.lock());
    if(!audio)
        return;

    const audio_output_info *aoi;
    aoi = audio->get_info();

    if (info->format == AUDIO_FORMAT_UNKNOWN)
        info->format = aoi->format;
    if (!info->samples_per_sec)
        info->samples_per_sec = aoi->samples_per_sec;
    if (info->speakers == SPEAKERS_UNKNOWN)
        info->speakers = aoi->speakers;

    get_info(info);
}

static void receive_audio(void *param, struct audio_data *data)
{
    aacEncoder *encoder = (aacEncoder *)param;

    if (!encoder->first_received) {
        encoder->first_raw_ts = data->timestamp;
        encoder->first_received = true;
        encoder->clear_audio();
    }

    if (!encoder->buffer_audio(data))
        return;

    while (encoder->audio_input_buffer.size >= encoder->audio_output_buffer.size())
        encoder->send_audio_data();
}

void aacEncoder::reset_audio_buffers()
{
    free_audio_buffers();
    audio_output_buffer.resize(blocksize * framesize,0);
}

void aacEncoder::clear_audio()
{
    audio_input_buffer.free();
}

void aacEncoder::send_audio_data()
{
    encoder_frame  enc_frame;

    audio_input_buffer.pop_front(&audio_output_buffer[0], audio_output_buffer.size());

    LOGI("send_audio_data----------------------------------------------------- %d",audio_input_buffer.size);
    enc_frame.data     = audio_output_buffer;
    enc_frame.frames = (uint32_t)framesize;
    enc_frame.pts    = cur_pts;

    do_encode(&enc_frame);

    cur_pts += framesize;
}

bool aacEncoder::buffer_audio(struct audio_data *data)
{
    size_t size = data->data.size();
    size_t offset_size = 0;
    bool success = true;

    do{
        if (!start_ts && (!paired_encoder.expired())) {
            uint64_t end_ts     = data->timestamp;
            uint64_t v_start_ts = paired_encoder.lock()->start_ts;

            /* no video yet, so don't start audio */
            if (!v_start_ts) {
                success = false;
                break;
            }

            /* audio starting point still not synced with video starting
             * point, so don't start audio */
            end_ts += (uint64_t)(data->data.size()/blocksize) * 1000000000ULL /
                      (uint64_t)samplerate;
            if (end_ts <= v_start_ts) {
                success = false;
                break;
            }

            /* ready to start audio, truncate if necessary */
            if (data->timestamp < v_start_ts)
                offset_size = calc_offset_size(v_start_ts,
                                               data->timestamp);
            if (data->timestamp <= v_start_ts)
                clear_audio();

            start_ts = v_start_ts;

            /* use currently buffered audio instead */
            if (v_start_ts < data->timestamp)
                start_from_buffer(v_start_ts);

        } else if (!start_ts && paired_encoder.expired()) {
            start_ts = data->timestamp;
        }
    }
    while(false);

    push_back_audio(data, size, offset_size);

    return success;
}

void aacEncoder::push_back_audio(struct audio_data *data, size_t size, size_t offset_size)
{
    size -= offset_size;

    if (size)
        audio_input_buffer.push_back(&data->data[offset_size], size);
}

void aacEncoder::free_audio_buffers()
{
    audio_input_buffer.free();
    audio_output_buffer.clear();
}

void aacEncoder::intitialize_audio_encoder()
{
    struct audio_convert_info info = {0};
    get_audio_info(&info);

    samplerate = info.samples_per_sec;
    blocksize  = get_audio_size(info.format, info.speakers, 1);
    framesize  = get_frame_size();

    reset_audio_buffers();
}

void aacEncoder::start_from_buffer(uint64_t v_start_ts)
{
    size_t size = audio_input_buffer.size;
    struct audio_data audio;
    size_t offset_size = 0;

    audio.data.resize(size,0);
    memcpy(&audio.data[0],audio_input_buffer.data,audio.data.size());
    clear_audio();

    if (first_raw_ts < v_start_ts)
        offset_size = calc_offset_size(v_start_ts, first_raw_ts);

    push_back_audio(&audio, size, offset_size);
}

size_t aacEncoder::calc_offset_size(uint64_t v_start_ts, uint64_t a_start_ts)
{
    uint64_t offset = v_start_ts - a_start_ts;
    offset = (uint64_t)offset * (uint64_t)samplerate /
             1000000000ULL;
    return (size_t)offset * blocksize;
}

void aacEncoder::on_initialize_internal()
{
    enc_create("aac", NULL);
    intitialize_audio_encoder();
}

void aacEncoder::init_sizes() {
    enum audio_format format;
    format = convert_ffmpeg_sample_format(context->sample_fmt);

    AudioOutput *audio = dynamic_cast<AudioOutput *>(media.lock().get());

    audio_planes = get_audio_planes(format, audio->info.speakers);
    audio_size = get_audio_size(format, audio->info.speakers, 1);
}

bool aacEncoder::initialize_codec() {
    int ret;

    aframe = av_frame_alloc();
    if (!aframe)
        return false;

    ret = avcodec_open2(context, codec, NULL);
    if (ret < 0)
        return false;

    aframe->format = context->sample_fmt;
    aframe->channels = context->channels;
    aframe->channel_layout = context->channel_layout;
    aframe->sample_rate = context->sample_rate;

    frame_size = context->frame_size;
    if (!frame_size)
        frame_size = 1024;

    frame_size_bytes = frame_size * (int) audio_size;

    ret = av_samples_alloc(&samples[0], NULL, context->channels,
                           frame_size, context->sample_fmt, 0);
    if (ret < 0)
        return false;

    return true;
}
