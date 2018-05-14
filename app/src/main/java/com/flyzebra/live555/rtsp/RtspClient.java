package com.flyzebra.live555.rtsp;

import android.net.Uri;

/**
 * Author: FlyZebra
 * Time: 18-5-13 下午6:55.
 * Discription: This is RtspClient
 */
public class RtspClient {
    private IRtspCallBack iRtspCallBack;
    static {
        System.loadLibrary("rtspclient");
    }

    public RtspClient(IRtspCallBack iRtspCallBack){
        this.iRtspCallBack = iRtspCallBack;
    }

    public void connect(String url){
        openUrl(url);
    }

    public void close(){
        stop();
    }

    private void onResult(String resultCode){
        if(iRtspCallBack!=null) iRtspCallBack.onResult(resultCode);
    }

    private void onVideo(byte[] videoBytes){
        if(iRtspCallBack!=null) iRtspCallBack.onVideo(videoBytes);
    }

    private void onAudio(byte[] audioBytes){
        if(iRtspCallBack!=null) iRtspCallBack.onAudio(audioBytes);
    }

    private void onSPS_PPS(byte[] sps,byte[] pps){
        if(iRtspCallBack!=null) iRtspCallBack.onSPS_PPS(sps,pps);
    }

    public native void openUrl(String url);

    public native void stop();

}
