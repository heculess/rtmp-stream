package com.heculess.rtmppush;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.Point;
import android.os.IBinder;
import android.view.WindowManager;

public class RecordService extends Service {

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate() {
        super.onCreate();

        WindowManager wm = (WindowManager) this
                .getSystemService(Context.WINDOW_SERVICE);
        Point window_size = new Point();
        wm.getDefaultDisplay().getSize(window_size);

        RtmpClient.init_video_info(window_size.x,window_size.y+80,ScreenRecorder.FRAME_RATE);
    }
    @Override
    public void onDestroy() {
        RtmpClient.close(0);
        super.onDestroy();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        RtmpClient.open("rtmp://192.168.1.33/live","push");
        return super.onStartCommand(intent, flags, startId);
    }



}
