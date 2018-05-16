package com.flyzebra.live555;

import android.os.Bundle;
import android.support.v7.app.AppCompatActivity;

import com.flyzebra.live555.rtsp.FlyLog;

public class MainActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        FlyLog.d("onCreate");
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
    }
}
