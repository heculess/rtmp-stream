package com.heculess.rtmppush;

import android.content.Context;
import android.graphics.Point;
import android.os.Handler;
import android.os.Message;
import android.support.v7.app.AppCompatActivity;
import android.content.Intent;
import android.os.Bundle;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.Toast;

public class MainActivity extends AppCompatActivity implements View.OnClickListener{
    private static MainActivity instance;
    private static final int REQUEST_CODE = 1;
    private MediaProjectionManager mMediaProjectionManager;
    private ScreenRecorder mVideoRecorder;
    private boolean isRecording = false;
    private Button mButton;
    Intent recorder_service = new Intent();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // Example of a call to a native method
        mMediaProjectionManager =
                (MediaProjectionManager) getSystemService(Context.MEDIA_PROJECTION_SERVICE);

        instance = this;
        mButton = (Button) findViewById(R.id.button);
        mButton.setOnClickListener(this);
    }

    Handler handler = new Handler(){
        @Override
        public void handleMessage(Message msg) {
            super.handleMessage(msg);

        }
    };
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

                WindowManager wm = (WindowManager) this
                        .getSystemService(Context.WINDOW_SERVICE);
                Point window_size = new Point();
                wm.getDefaultDisplay().getSize(window_size);
                mVideoRecorder = new ScreenRecorder(window_size.x,window_size.y+80, 6000, 1, mediaProjection);
                mVideoRecorder.start();
                mButton.setText("Stop");
                Toast.makeText(this, "Screen recorder is running...", Toast.LENGTH_SHORT).show();
                //moveTaskToBack(true);
            }
        }

    }

    public void onClick(View v) {
        if (isRecording == true && mVideoRecorder != null) {
            stopScreenRecord(1);
            stopService(recorder_service);
        } else {
            recorder_service.setAction("com.rtmp.recordservice");
            recorder_service.setPackage(getPackageName());
            startService(recorder_service);

            createScreenCapture();
        }
    }

    public void createScreenCapture() {
        isRecording = true;
        Intent captureIntent = mMediaProjectionManager.createScreenCaptureIntent();
        startActivityForResult(captureIntent, REQUEST_CODE);
    }

    public void stopScreenRecord(int i) {
        mVideoRecorder.quit();
        mVideoRecorder = null;
        isRecording = false;
        if(i != 3){
            mButton.setText("Restart recorder");
        }
    }


}
