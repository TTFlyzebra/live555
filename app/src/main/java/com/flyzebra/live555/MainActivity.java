package com.flyzebra.live555;

import android.os.Bundle;
import android.support.v7.app.AppCompatActivity;
import android.view.SurfaceView;
import android.widget.TextView;

import com.flyzebra.live555.rtsp.FlyLog;
import com.flyzebra.live555.rtsp.IRtspCallBack;
import com.flyzebra.live555.rtsp.RtspClient;

public class MainActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
    }



}
