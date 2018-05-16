package com.flyzebra.live555.rtsp;

import android.content.Context;
import android.util.AttributeSet;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import com.flyzebra.live555.decoder.MediaDecoder;

/**
 * Author: FlyZebra
 * Time: 18-5-14 下午9:00.
 * Discription: This is RtspVideoView
 */
public class RtspVideoView extends SurfaceView implements SurfaceHolder.Callback{
    private RtspClient rtspClient;
    private MediaDecoder mediaDecoder;
    public RtspVideoView(Context context) {
        this(context,null);
    }

    public RtspVideoView(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public RtspVideoView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init(context);
    }

    private void init(Context context) {
        getHolder().addCallback(this);
    }

    @Override
    public void surfaceCreated(SurfaceHolder surfaceHolder) {
        mediaDecoder = new MediaDecoder(getHolder().getSurface());
        rtspClient = new RtspClient(new IRtspCallBack() {
            @Override
            public void onResult(String resultCode) {
            }

            @Override
            public void onVideo(byte[] videoBytes) {
                FlyLog.i("size=%d:%s",videoBytes.length,bytes2HexString(videoBytes));
                mediaDecoder.input(videoBytes,videoBytes.length);
            }

            @Override
            public void onAudio(byte[] audioBytes) {

            }

            @Override
            public void onSPS_PPS(byte[] sps, byte[] pps) {
                mediaDecoder.initMediaCodec(sps,pps);
                mediaDecoder.start();
                FlyLog.i("sps=%s,pps=%s", bytes2HexString(sps),bytes2HexString(pps));
            }
        });

        new Thread(new Runnable() {
            @Override
            public void run() {
                rtspClient.openUrl("rtsp://192.168.42.1/live");
            }
        }).start();
    }

    @Override
    public void surfaceChanged(SurfaceHolder surfaceHolder, int i, int i1, int i2) {

    }

    @Override
    public void surfaceDestroyed(SurfaceHolder surfaceHolder) {
        rtspClient.stop();
        mediaDecoder.stop();
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
