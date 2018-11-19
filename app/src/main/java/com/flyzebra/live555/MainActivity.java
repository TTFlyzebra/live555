package com.flyzebra.live555;

import android.os.Bundle;
import android.support.v7.app.AppCompatActivity;

import com.flyzebra.live555.rtsp.RtspVideoView;

public class MainActivity extends AppCompatActivity {
    private RtspVideoView rtspVideoView;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        rtspVideoView = findViewById(R.id.ac_main_rtspsv);
        rtspVideoView.setRtspUrl("rtsp://192.168.1.88/live");

    }
}
