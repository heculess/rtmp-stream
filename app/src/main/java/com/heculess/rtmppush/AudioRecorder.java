package com.heculess.rtmppush;

import android.media.MediaCodec;
import android.media.MediaFormat;
import android.util.Log;

import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicBoolean;

import static android.content.ContentValues.TAG;

public class AudioRecorder extends Thread {
    private static final long WAIT_TIME = 5000;
    private AtomicBoolean mQuit = new AtomicBoolean(false);
    private MediaCodec dstAudioEncoder;
    private MediaCodec.BufferInfo eInfo;
    private long startTime = 0;
    @Override
    public void run() {
        while (!mQuit.get()) {
            int eobIndex = dstAudioEncoder.dequeueOutputBuffer(eInfo, WAIT_TIME);
            switch (eobIndex) {
                case MediaCodec.INFO_TRY_AGAIN_LATER:
//                        LogTools.d("AudioSenderThread,MediaCodec.INFO_TRY_AGAIN_LATER");
                    break;
                case MediaCodec.INFO_OUTPUT_FORMAT_CHANGED:
                    Log.d(TAG, "AudioSenderThread,MediaCodec.INFO_OUTPUT_FORMAT_CHANGED:" +
                            dstAudioEncoder.getOutputFormat().toString());
                    sendAudioSpecificConfig(dstAudioEncoder.getOutputFormat());
                    break;
                default:
                    Log.d(TAG, "AudioSenderThread,MediaCode,eobIndex=" + eobIndex);

                    if (eInfo.flags != MediaCodec.BUFFER_FLAG_CODEC_CONFIG && eInfo.size >= 0 && eobIndex >= 0) {
                        ByteBuffer realData = dstAudioEncoder.getOutputBuffer(eobIndex);
                        realData.position(eInfo.offset);
                        realData.limit(eInfo.offset + eInfo.size);
                        sendRealData(eInfo.presentationTimeUs, realData);
                    }
                    dstAudioEncoder.releaseOutputBuffer(eobIndex, false);
                    break;
            }
        }
        eInfo = null;
    }

    private void sendAudioSpecificConfig(MediaFormat format) {
        RtmpClient.initAudioHeader(format.getByteBuffer("csd-0").array());
    }

    private void sendRealData(long tms, ByteBuffer realData){
        ByteBuffer pushData = ByteBuffer.allocate(realData.remaining());
        pushData.put(realData);
        pushData.flip();
        RtmpClient.pushAudioData(tms*1000,pushData.array());
    }


}
