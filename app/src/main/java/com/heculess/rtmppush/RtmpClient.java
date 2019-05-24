package com.heculess.rtmppush;

public class RtmpClient {

    static {
        System.loadLibrary("native-lib");
    }

    public static native long open(String url,String name);
    public static native int close(long rtmpPointer);
    public static native void init_video_info(int width, int height, int fps);

    public static native void pushAudioData(long tms, byte[] data);
    public static native void initAudioHeader(byte[] csd0);

    public static native void pushVideoData(long tms, byte[] data);
    public static native void initVideoHeader(byte[] csd0,byte[] csd1);
}
