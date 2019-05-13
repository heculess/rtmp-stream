#include <jni.h>
#include <string>

# include "rtmp-push.h"
#include "rtmp-struct.h"

static RtmpPush* pusher = NULL;

extern "C" JNIEXPORT jstring JNICALL
Java_com_heculess_rtmppush_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}
extern "C"
JNIEXPORT jlong JNICALL
Java_com_heculess_rtmppush_MainActivity_open(JNIEnv *env, jobject instance, jstring url_,
                                             jstring name_) {
    const char *url = env->GetStringUTFChars(url_, 0);
    const char *name = env->GetStringUTFChars(name_, 0);

    // TODO
    if(!pusher)
        pusher = new RtmpPush;

    jlong ret = 0;

    pusher->streamUrl = url;
    pusher->streamName = name;

    if(!pusher->StartStreaming(pusher->streamUrl.c_str(),
                               pusher->streamName.c_str()))
        ret = -1;

    env->ReleaseStringUTFChars(url_, url);
    env->ReleaseStringUTFChars(name_, name);
    return ret;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_heculess_rtmppush_MainActivity_close(JNIEnv *env, jobject instance, jlong rtmpPointer) {

    // TODO
    if(pusher)
        pusher->StopStreaming();
    return 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_heculess_rtmppush_ScreenRecorder_pushVideoData(JNIEnv *env, jobject instance, jlong tms,
                                                        jbyteArray data_) {
    jbyte *buffer = env->GetByteArrayElements(data_, NULL);
    jsize  oldsize = env->GetArrayLength(data_);
    // TODO

    if(pusher){
        video_data videodata;
        videodata.data.resize(oldsize,0);
        memcpy(&videodata.data[0],buffer,videodata.data.size());
        videodata.timestamp = tms;

        pusher->Push_video_data(&videodata);
    }

    env->ReleaseByteArrayElements(data_, buffer, 0);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_heculess_rtmppush_MainActivity_init_1video_1info(JNIEnv *env, jobject instance, jint width,
                                                          jint height, jint color_space, jint fps) {

    // TODO
    if(!pusher)
        pusher = new RtmpPush;

    pusher->video_info.width = width;
    pusher->video_info.height = height;
    pusher->video_info.fps_num = fps;
}
extern "C"
JNIEXPORT void JNICALL
Java_com_heculess_rtmppush_ScreenRecorder_initVideoHeader(JNIEnv *env, jobject instance,
                                                          jbyteArray csd0_, jbyteArray csd1_) {
    jbyte *csd0 = env->GetByteArrayElements(csd0_, NULL);
    jbyte *csd1 = env->GetByteArrayElements(csd1_, NULL);

    jsize  csdsize0 = env->GetArrayLength(csd0_);
    jsize  csdsize1 = env->GetArrayLength(csd1_);

    if(pusher){

        std::shared_ptr<VideoOutput> video_output =
                std::dynamic_pointer_cast<VideoOutput>(pusher->video);

        video_output->format_csd0.resize(csdsize0,0);
        video_output->format_csd1.resize(csdsize1,0);

        memcpy(&video_output->format_csd0[0], csd0, video_output->format_csd0.size());
        memcpy(&video_output->format_csd1[0], csd1, video_output->format_csd1.size());

    }
    // TODO

    env->ReleaseByteArrayElements(csd0_, csd0, 0);
    env->ReleaseByteArrayElements(csd1_, csd1, 0);
}