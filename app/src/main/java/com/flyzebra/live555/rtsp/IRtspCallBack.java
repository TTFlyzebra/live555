package com.flyzebra.live555.rtsp;

/**
 * Author: FlyZebra
 * Time: 18-5-13 下午7:09.
 * Discription: This is IRtspCallBack
 */
public interface IRtspCallBack {
    void onResult(String resultCode);

    void onVideo(byte[] videoBytes);

    void onAudio(byte[] audioBytes);

    void onRecvRTP(byte[] sps,byte[] pps);

    void onStop();
}
