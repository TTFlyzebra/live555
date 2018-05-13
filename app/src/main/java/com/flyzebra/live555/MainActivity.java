package com.flyzebra.live555;

import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.widget.TextView;

import com.flyzebra.live555.rtsp.IRtspCallBack;
import com.flyzebra.live555.rtsp.RtspClient;

public class MainActivity extends AppCompatActivity {
    private TextView tv;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        tv = (TextView) findViewById(R.id.sample_text);
        RtspClient rtspClient = new RtspClient(new IRtspCallBack() {
            @Override
            public void onResult(String resultCode) {
//                tv.setText(resultCode);
            }

            @Override
            public void onVideo(byte[] videoBytes) {
                tv.setText(new String(videoBytes));
            }

            @Override
            public void onAudio(byte[] audioBytes) {

            }
        });

        rtspClient.openUrl("rtsp://192.168.1.88:8554/test.mkv");

    }

}
