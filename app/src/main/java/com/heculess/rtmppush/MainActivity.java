package com.heculess.rtmppush;

import android.content.Context;
import android.graphics.Point;
import android.support.v7.app.AppCompatActivity;
import android.content.Intent;
import android.os.Bundle;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.util.Log;
import android.view.WindowManager;

public class MainActivity extends AppCompatActivity {
    private static MainActivity instance;
    private static final int REQUEST_CODE = 1;
    private MediaProjectionManager mMediaProjectionManager;
    private Point window_size;
    private ScreenRecorder mVideoRecorder;

    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("native-lib");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // Example of a call to a native method
        mMediaProjectionManager =
                (MediaProjectionManager) getSystemService(Context.MEDIA_PROJECTION_SERVICE);

        instance = this;

        WindowManager wm = (WindowManager) this
                .getSystemService(Context.WINDOW_SERVICE);
        window_size = new Point();
        wm.getDefaultDisplay().getSize(window_size);

        init_video_info(window_size.x,window_size.y+80,2,60);
        open("rtmp://192.168.1.33/live","push");

        Intent captureIntent = mMediaProjectionManager.createScreenCaptureIntent();
        startActivityForResult(captureIntent, REQUEST_CODE);
    }

    /**
     * A native method that is implemented by the 'native-lib' native library,
     * which is packaged with this application.
     */

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if(requestCode == REQUEST_CODE) {
            if(resultCode == RESULT_OK) {
                MediaProjection mediaProjection = mMediaProjectionManager.getMediaProjection(resultCode, data);
                if (mediaProjection == null) {
                    Log.e("@@", "media projection is null");
                    return;
                }

                mVideoRecorder = new ScreenRecorder(window_size.x,window_size.y+80, 6000, 1, mediaProjection);
                mVideoRecorder.start();
            }
        }

    }

    public native long open(String url,String name);
    public native int close(long rtmpPointer);

    public native void init_video_info(int width, int height, int color_space, int fps);
}
