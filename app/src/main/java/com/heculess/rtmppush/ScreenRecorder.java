package com.heculess.rtmppush;

import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.projection.MediaProjection;
import android.util.Log;
import android.view.Surface;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicBoolean;

public class ScreenRecorder extends Thread {
    private static final String TAG = "ScreenRecorder";

    private int mWidth;
    private int mHeight;
    private int mBitRate;
    private int mDpi;
    private MediaProjection mMediaProjection;

    private static final int FRAME_RATE = 60;
    private static final int IFRAME_INTERVAL = 2;
    private static final int TIMEOUT_US = 10000;

    private MediaCodec mEncoder;
    private Surface mSurface;
    private AtomicBoolean mQuit = new AtomicBoolean(false);
    private MediaCodec.BufferInfo mBufferInfo = new MediaCodec.BufferInfo();
    private VirtualDisplay mVirtualDisplay;



    public ScreenRecorder(int width, int height, int bitrate, int dpi, MediaProjection mp) {
        super(TAG);
        mWidth = width;
        mHeight = height;
        mBitRate = bitrate;
        mDpi = dpi;
        mMediaProjection = mp;
    }

    public final void quit() {
        if(mQuit != null){
            mQuit.set(true);
        }
    }

    @Override
    public void run() {
        try {
            try {
                prepareEncoder();
            } catch (IOException e) {
                throw new RuntimeException(e);
            }
            mVirtualDisplay = mMediaProjection.createVirtualDisplay(TAG + "-display",
                    mWidth, mHeight, mDpi, DisplayManager.VIRTUAL_DISPLAY_FLAG_PUBLIC,
                    mSurface, null, null);
            Log.d(TAG, "created virtual display: " + mVirtualDisplay);
            recordVirtualDisplay();
        } catch (Exception e) {
            e.printStackTrace();
        } finally {
            release();
        }
    }

    private void prepareEncoder() throws IOException {
        MediaFormat format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC,
                mWidth, mHeight);
        format.setInteger(MediaFormat.KEY_COLOR_FORMAT,
                MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
        format.setInteger(MediaFormat.KEY_BIT_RATE, 6000000);
        format.setInteger(MediaFormat.KEY_BITRATE_MODE,
                MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR);
        format.setInteger(MediaFormat.KEY_FRAME_RATE, FRAME_RATE);
        format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, IFRAME_INTERVAL);
        mEncoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC);
        mEncoder.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
        mSurface = mEncoder.createInputSurface();
        mEncoder.start();
    }

    private void recordVirtualDisplay() {
        while (!mQuit.get()) {
            int eobIndex = mEncoder.dequeueOutputBuffer(mBufferInfo, TIMEOUT_US);
            switch (eobIndex) {
                case MediaCodec.INFO_TRY_AGAIN_LATER:
                    break;
                case MediaCodec.INFO_OUTPUT_FORMAT_CHANGED:
                    Log.d(TAG, "VideoSenderThread,MediaCodec.INFO_OUTPUT_FORMAT_CHANGED:" +
                            mEncoder.getOutputFormat().toString());
                    sendAVCDecoderHeader(mEncoder.getOutputFormat());
                    break;
                default:
                    Log.d(TAG, "VideoSenderThread,MediaCode,eobIndex=" + eobIndex);
                    if (mBufferInfo.flags != MediaCodec.BUFFER_FLAG_CODEC_CONFIG && mBufferInfo.size != 0 && eobIndex > 0) {
                        ByteBuffer realData = mEncoder.getOutputBuffer(eobIndex);
                        sendRealData(mBufferInfo.presentationTimeUs, realData);
                    }
                    mEncoder.releaseOutputBuffer(eobIndex, false);
                    break;
            }
        }
    }

    private void release() {
        if (mEncoder != null) {
            mEncoder.stop();
            mEncoder.release();
            mEncoder = null;
        }
        if (mVirtualDisplay != null) {
            mVirtualDisplay.release();
        }
        if (mMediaProjection != null) {
            mMediaProjection.stop();
        }
    }

    private void sendAVCDecoderHeader(MediaFormat format) {

        ByteBuffer SPSByteBuff = format.getByteBuffer("csd-0");
        ByteBuffer PPSByteBuff = format.getByteBuffer("csd-1");

        initVideoHeader(SPSByteBuff.array(), PPSByteBuff.array());
    }

    private void sendRealData(long tms, ByteBuffer realData){
        ByteBuffer pushData = ByteBuffer.allocate(realData.remaining());
        pushData.put(realData);
        pushData.flip();
        pushVideoData(tms*1000,pushData.array());
    }

    public native void pushVideoData(long tms, byte[] data);
    public native void initVideoHeader(byte[] csd0,byte[] csd1);
}
