package com.flyzebra.live555;

import android.os.Bundle;
import android.support.v7.app.AppCompatActivity;
import android.widget.TextView;

import com.flyzebra.live555.rtsp.FlyLog;
import com.flyzebra.live555.rtsp.IRtspCallBack;
import com.flyzebra.live555.rtsp.RtspClient;

public class MainActivity extends AppCompatActivity {
    private TextView tv;
    private RtspClient rtspClient;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        tv = (TextView) findViewById(R.id.sample_text);
        rtspClient = new RtspClient(new IRtspCallBack() {
            @Override
            public void onResult(String resultCode) {
                tv.setText(resultCode);
            }

            @Override
            public void onVideo(byte[] videoBytes) {
                FlyLog.i(bytes2HexString(videoBytes));
            }

            @Override
            public void onAudio(byte[] audioBytes) {

            }
        });

        new Thread(new Runnable() {
            @Override
            public void run() {
                rtspClient.openUrl("rtsp://192.168.42.1/live");
            }
        }).start();

    }

    public static String bytes2HexString(byte[] bytes){
        if(bytes==null||bytes.length==1){
            return null;
        }

        StringBuilder stringBuffer = new StringBuilder("");
        for (byte aByte : bytes) {
            String hv = Integer.toHexString(aByte & 0xFF);
            if (hv.length() < 2) {
                stringBuffer.append(0);
            }
            stringBuffer.append(hv).append("-");
        }

        return stringBuffer.toString();
    }

}
