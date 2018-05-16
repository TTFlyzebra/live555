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
public class RtspVideoView extends SurfaceView implements SurfaceHolder.Callback {
    private RtspClient rtspClient;
    private MediaDecoder mediaDecoder;
    private boolean isHasSurface = false;

    public RtspVideoView(Context context) {
        this(context, null);
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
        isHasSurface = true;
        rtspClient = new RtspClient(new IRtspCallBack() {
            @Override
            public void onResult(String resultCode) {
                FlyLog.d("rtspclient message:%s", resultCode);
            }

            @Override
            public void onVideo(byte[] videoBytes) {
                if(isHasSurface) mediaDecoder.input(videoBytes);
            }

            @Override
            public void onAudio(byte[] audioBytes) {

            }

            @Override
            public void onRecvRTP(byte[] sps, byte[] pps) {
                mediaDecoder = new MediaDecoder(getHolder().getSurface(),sps,pps);
            }

            @Override
            public void onStop() {
                mediaDecoder.stop();
            }
        });
        rtspClient.connect("rtsp://192.168.42.1/live");
    }

    @Override
    public void surfaceChanged(SurfaceHolder surfaceHolder, int i, int i1, int i2) {
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder surfaceHolder) {
        isHasSurface = false;
        mediaDecoder.stop();
        rtspClient.close();
        mediaDecoder = null;
        rtspClient = null;
    }
}
