
#pragma once

#include <string>
#include <vector>
#include <memory>

#include "callback/proc.h"
#include "callback/signal.h"
#include "util/circlebuf.h"
#include "util/threading.h"


#define MAJOR_VER  1
#define MINOR_VER  0
#define PATCH_VER  1


#define AUDIO_OUTPUT_FRAMES 1024
#define MAX_CONVERT_BUFFERS 3

/** Specifies the encoder type */
enum obs_encoder_type {
    OBS_ENCODER_AUDIO, /**< The encoder provides an audio codec */
    OBS_ENCODER_VIDEO  /**< The encoder provides a video codec */
};

enum audio_format {
    AUDIO_FORMAT_UNKNOWN,

    AUDIO_FORMAT_U8BIT,
    AUDIO_FORMAT_16BIT,
    AUDIO_FORMAT_32BIT,
    AUDIO_FORMAT_FLOAT,

    AUDIO_FORMAT_U8BIT_PLANAR,
    AUDIO_FORMAT_16BIT_PLANAR,
    AUDIO_FORMAT_32BIT_PLANAR,
    AUDIO_FORMAT_FLOAT_PLANAR,
};

enum speaker_layout {
    SPEAKERS_UNKNOWN,   /**< Unknown setting, fallback is stereo. */
    SPEAKERS_MONO,      /**< Channels: MONO */
    SPEAKERS_STEREO,    /**< Channels: FL, FR */
    SPEAKERS_2POINT1,   /**< Channels: FL, FR, LFE */
    SPEAKERS_4POINT0,   /**< Channels: FL, FR, FC, RC */
    SPEAKERS_4POINT1,   /**< Channels: FL, FR, FC, LFE, RC */
    SPEAKERS_5POINT1,   /**< Channels: FL, FR, FC, LFE, RL, RR */
    SPEAKERS_7POINT1=8, /**< Channels: FL, FR, FC, LFE, RL, RR, SL, SR */
};

struct audio_convert_info {
    uint32_t            samples_per_sec;
    enum audio_format   format;
    enum speaker_layout speakers;
};

enum video_format {
    VIDEO_FORMAT_NONE,
    /* planar 420 format */
    VIDEO_FORMAT_I420, /* three-plane */
    VIDEO_FORMAT_NV12, /* two-plane, luma and packed chroma */

    /* packed 422 formats */
    VIDEO_FORMAT_YVYU,
    VIDEO_FORMAT_YUY2, /* YUYV */
    VIDEO_FORMAT_UYVY,

    /* packed uncompressed formats */
    VIDEO_FORMAT_RGBA,
    VIDEO_FORMAT_BGRA,
    VIDEO_FORMAT_BGRX,
    VIDEO_FORMAT_Y800, /* grayscale */

    /* planar 4:4:4 */
    VIDEO_FORMAT_I444,
};

enum video_range_type {
    VIDEO_RANGE_DEFAULT,
    VIDEO_RANGE_PARTIAL,
    VIDEO_RANGE_FULL
};

enum video_colorspace {
    VIDEO_CS_DEFAULT,
    VIDEO_CS_601,
    VIDEO_CS_709,
};

enum obs_obj_type {
    OBS_OBJ_TYPE_INVALID,
    OBS_OBJ_TYPE_SOURCE,
    OBS_OBJ_TYPE_OUTPUT,
    OBS_OBJ_TYPE_ENCODER,
    OBS_OBJ_TYPE_SERVICE
};

struct video_scale_info {
    enum video_format     format = VIDEO_FORMAT_NONE;
    uint32_t              width = 0;
    uint32_t              height = 0;
    enum video_range_type range = VIDEO_RANGE_DEFAULT;
    enum video_colorspace colorspace = VIDEO_CS_DEFAULT;
};

struct media_data {
    std::vector<uint8_t> data;
    uint64_t            timestamp = 0;
};

struct audio_output_data {
    audio_output_data()
    {
        data.resize(AUDIO_OUTPUT_FRAMES,0);
    }

    std::vector<float>  data;
};

class media_output
{
public:
    media_output();
    virtual ~media_output();
    bool output_open();
    void output_close();

    void update_input_frame(media_data &input_frame);

protected:
    pthread_t thread;
    os_sem_t *update_semaphore;
    pthread_mutex_t data_mutex;
    pthread_mutex_t input_mutex;
    bool  stop;
    bool  initialized;
    media_data cache;

    virtual void on_media_thread_create(){};

    bool output_cur_frame();
    void output_unlock_frame();

    virtual void on_input_mutex(media_data &frame){}

private:
    static void *media_thread(void *param);

    void media_thread_run();
};

struct encoder_packet_info
{
    encoder_packet_info();
    uint8_t               *data_ptr;
    int64_t               data_size;
    int64_t               pts;          /**< Presentation timestamp */
    int64_t               dts;          /**< Decode timestamp */

    int32_t               timebase_num; /**< Timebase numerator */
    int32_t               timebase_den; /**< Timebase denominator */

    enum obs_encoder_type type;         /**< Encoder type */

    bool                  keyframe;     /**< Is a keyframe */

    int64_t               dts_usec;
    int64_t               sys_dts_usec;

    int                   priority;
    int                   drop_priority;

    size_t                track_idx ;
};
/** Encoder output packet */
class encoder_packet : public  encoder_packet_info{
public:
    encoder_packet();
    encoder_packet(encoder_packet_info & info);

    virtual  ~encoder_packet();

    std::vector<uint8_t>  data;

    void create_instance(encoder_packet &dst);
    void packet_release();

    int32_t get_ms_time(int64_t val);
    int64_t get_dts_usec();

    encoder_packet_info *serialize_to();
    int64_t get_serialize_size();
    void serialize_from(encoder_packet_info &info);

private:
    bool is_attach_info;
};

struct encoder_callback {
    bool sent_first_packet;
    void (*new_packet)(void *param, encoder_packet &packet);
    void *param;
};

typedef void (*encoded_callback_t)(void *data, encoder_packet &packet);
typedef void (*audio_output_callback_t)(void *param, media_data &data);

struct audio_output_info {
    audio_output_info():
    samples_per_sec(0),
    format(AUDIO_FORMAT_UNKNOWN),
    speakers(SPEAKERS_UNKNOWN)
    {}

    std::string         name;

    uint32_t            samples_per_sec;
    enum audio_format   format;
    enum speaker_layout speakers;
};

struct encoder_frame {
    std::vector<uint8_t>  data;
    uint32_t              frames = 0;
    int64_t               pts = 0;
};

struct video_output_info {
    video_output_info():
    format(VIDEO_FORMAT_NONE),
    colorspace(VIDEO_CS_DEFAULT),
    range(VIDEO_RANGE_DEFAULT),
    fps_num(0),
    fps_den(0),
    width(0),
    height(0),
    samples_per_sec(0)
    {}
    std::string       name;

    enum video_format format;
    enum video_colorspace colorspace;
    enum video_range_type range;

    uint32_t          fps_num ;
    uint32_t          fps_den;
    uint32_t          width;
    uint32_t          height;
    uint64_t          samples_per_sec;
};

struct video_input {
    video_scale_info   conversion;
    void (*callback)(void *param, struct media_data *frame) = NULL;
    void *param = NULL;
    uint64_t last_output_timestamp = 0;
};

struct audio_resampler;
typedef struct audio_resampler audio_resampler_t;

struct audio_input {
    struct audio_convert_info conversion;
    //audio_resampler_t         *resampler = NULL;
    audio_output_callback_t callback;
    void *param = NULL;
};

struct audio_mix {
    std::vector<audio_input> inputs;
    float buffer[AUDIO_OUTPUT_FRAMES];
};



/** Maximum number of source channels for output and per display */

#define OBS_OUTPUT_SUCCESS         0
#define OBS_OUTPUT_BAD_PATH       -1
#define OBS_OUTPUT_CONNECT_FAILED -2
#define OBS_OUTPUT_INVALID_STREAM -3
#define OBS_OUTPUT_ERROR          -4
#define OBS_OUTPUT_DISCONNECTED   -5

