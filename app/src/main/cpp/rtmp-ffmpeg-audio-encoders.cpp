
# include "rtmp-encoder.h"

# include "rtmp-audio-output.h"

static void receive_audio(void *param, struct media_data &data);

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

aacEncoder::aacEncoder():
samplerate(0),
blocksize(0),
total_samples(0),
audio_planes(0),
audio_size(0),
framesize(1024),
frame_size_bytes(0)
{
    id = "ffmpeg_aac";
    type = OBS_ENCODER_AUDIO;
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

bool aacEncoder::encode(encoder_frame &frame,
            encoder_packet &packet, bool &received_packet) {

    received_packet = true;
    packet.type = OBS_ENCODER_AUDIO;
    packet.data = frame.data;
    packet.pts  = frame.pts;
    packet.dts  = frame.pts;

    return true;
}

bool aacEncoder::get_extra_data(std::vector<uint8_t> &data){
    if(extra_data.size() == 0)
        load_headers();

    data = extra_data;
    return true;
}

std::vector<uint8_t> aacEncoder::get_encode_header()
{
    std::vector<uint8_t> header;
    get_extra_data(header);
    return header;
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

void aacEncoder::add_connection()
{
    audio_convert_info audio_info = {0};
    get_audio_info(audio_info);

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

void aacEncoder::get_audio_info(audio_convert_info &info)
{
    std::shared_ptr<AudioOutput> audio =
            std::dynamic_pointer_cast<AudioOutput>(media.lock());
    if(!audio)
        return;

    const audio_output_info *aoi;
    aoi = audio->get_info();

    if (info.format == AUDIO_FORMAT_UNKNOWN)
        info.format = aoi->format;
    if (!info.samples_per_sec)
        info.samples_per_sec = aoi->samples_per_sec;
    if (info.speakers == SPEAKERS_UNKNOWN)
        info.speakers = aoi->speakers;
}

static void receive_audio(void *param, media_data &data)
{
    aacEncoder *encoder = (aacEncoder *)param;

    if (!encoder->first_received) {
        encoder->first_raw_ts = data.timestamp;
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
    encoder_frame enc_frame;

    audio_input_buffer.pop_front(&audio_output_buffer[0], audio_output_buffer.size());

    enc_frame.data   = audio_output_buffer;
    enc_frame.frames = (uint32_t)framesize;
    enc_frame.pts    = cur_pts;

    do_encode(enc_frame);

    cur_pts += framesize;
}

bool aacEncoder::buffer_audio(media_data &data)
{
    size_t size = data.data.size();
    size_t offset_size = 0;
    bool success = true;

    do{
        if (!start_ts && (!paired_encoder.expired())) {
            uint64_t end_ts     = data.timestamp;
            uint64_t v_start_ts = paired_encoder.lock()->start_ts;

            /* no video yet, so don't start audio */
            if (!v_start_ts) {
                success = false;
                break;
            }

            /* audio starting point still not synced with video starting
             * point, so don't start audio */
            end_ts += (uint64_t)(data.data.size()/blocksize) * 1000000000ULL /
                      (uint64_t)samplerate;
            if (end_ts <= v_start_ts) {
                success = false;
                break;
            }

            /* ready to start audio, truncate if necessary */
            if (data.timestamp < v_start_ts)
                offset_size = calc_offset_size(v_start_ts,
                                               data.timestamp);
            if (data.timestamp <= v_start_ts)
                clear_audio();

            start_ts = v_start_ts;

            /* use currently buffered audio instead */
            if (v_start_ts < data.timestamp)
                start_from_buffer(v_start_ts);

        } else if (!start_ts && paired_encoder.expired()) {
            start_ts = data.timestamp;
        }
    }
    while(false);

    push_back_audio(data, size, offset_size);

    return success;
}

void aacEncoder::push_back_audio(media_data &data, size_t size, size_t offset_size)
{
    size -= offset_size;

    if (size)
        audio_input_buffer.push_back(&data.data[offset_size], size);
}

void aacEncoder::free_audio_buffers()
{
    audio_input_buffer.free();
    audio_output_buffer.clear();
}

void aacEncoder::intitialize_audio_encoder()
{
    audio_convert_info info = {0};
    get_audio_info(info);

    samplerate = info.samples_per_sec;
    blocksize  = get_audio_size(info.format, info.speakers, 1);

    reset_audio_buffers();
}

void aacEncoder::start_from_buffer(uint64_t v_start_ts)
{
    size_t size = audio_input_buffer.size;
    media_data audio;
    size_t offset_size = 0;

    audio.data.resize(size,0);
    memcpy(&audio.data[0],audio_input_buffer.data,audio.data.size());
    clear_audio();

    if (first_raw_ts < v_start_ts)
        offset_size = calc_offset_size(v_start_ts, first_raw_ts);

    push_back_audio(audio, size, offset_size);
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
    intitialize_audio_encoder();
}

void aacEncoder::load_headers()
{
    std::shared_ptr<AudioOutput> audio =
            std::dynamic_pointer_cast<AudioOutput>(media.lock());
    if(!audio)
        return;

    extra_data.resize(audio->format.size(),0);
    memcpy(&extra_data[0], (void*)&audio->format[0], extra_data.size());
}

void aacEncoder::on_actually_destroy()
{
    free_audio_buffers();
}

