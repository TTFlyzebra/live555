package com.flyzebra.live555.decoder;

import android.media.MediaCodec;
import android.media.MediaFormat;
import android.view.Surface;

import com.flyzebra.live555.rtsp.FlyLog;

import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * Created by FlyZebra on 2018/5/11.
 * Descrip:
 */

public class MediaDecoder {
    private MediaCodec mediaCodec;
    private Surface surface;
    private ByteBuffer[] inputBuffers;
    private ByteBuffer[] outputBuffers;
    private MediaCodec.BufferInfo info;
    private MediaFormat mediaFormat;
    private long BUFFER_TIMEOUT = -1;

    public MediaDecoder(Surface surface, byte[] sps, byte[] pps) {
        this.surface = surface;
        initMediaCodec(sps, pps);
        start();
    }

    private void initMediaCodec(byte[] sps, byte[] pps) {
        try {
            info = new MediaCodec.BufferInfo();
            mediaFormat = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, 1280, 720);
            mediaFormat.setByteBuffer("csd-0", ByteBuffer.wrap(sps));
            mediaFormat.setByteBuffer("csd-1", ByteBuffer.wrap(pps));
            mediaCodec = MediaCodec.createDecoderByType("video/avc");
            mediaCodec.configure(mediaFormat, surface, null, 0);
        } catch (IOException e) {
            FlyLog.e("create MediaCodec error e=%s", e.toString());
            e.printStackTrace();
        }
    }

    public void start() {
        mediaCodec.start();
        inputBuffers = mediaCodec.getInputBuffers();
        outputBuffers = mediaCodec.getOutputBuffers();
    }

    public void input(byte[] data) {
        int inIndex = mediaCodec.dequeueInputBuffer(BUFFER_TIMEOUT);
        if (inIndex >= 0) {
            ByteBuffer buffer = inputBuffers[inIndex];
            buffer.clear();
            buffer.put(data);
            mediaCodec.queueInputBuffer(inIndex, 0, data.length, 33, 0);
        }
        int outIndex = mediaCodec.dequeueOutputBuffer(info, 0);
        switch (outIndex) {
            case MediaCodec.INFO_OUTPUT_FORMAT_CHANGED:
                inputBuffers = mediaCodec.getInputBuffers();
                break;
            case MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED:
                outputBuffers = mediaCodec.getOutputBuffers();
                break;
            case MediaCodec.INFO_TRY_AGAIN_LATER:
                break;
            default:
                mediaCodec.releaseOutputBuffer(outIndex, true);
                break;
        }
    }

    /**
     * 输出编码后的数据
     *
     * @param data 数据
     * @param len  有效数据长度
     * @param ts   时间戳
     */
    public void output(/*out*/byte[] data,/* out */int[] len,/* out */long[] ts) {
        int outIndex = mediaCodec.dequeueOutputBuffer(info, BUFFER_TIMEOUT);
        if (outIndex >= 0) {
            outputBuffers[outIndex].position(info.offset);
            outputBuffers[outIndex].limit(info.offset + info.size);
            outputBuffers[outIndex].get(data, 0, info.size);
            len[0] = info.size;
            ts[0] = info.presentationTimeUs;
            mediaCodec.releaseOutputBuffer(outIndex, false);
        }

        switch (outIndex) {
            case MediaCodec.INFO_OUTPUT_FORMAT_CHANGED:
                inputBuffers = mediaCodec.getInputBuffers();
                break;
            case MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED:
                outputBuffers = mediaCodec.getOutputBuffers();
                break;
            case MediaCodec.INFO_TRY_AGAIN_LATER:
                break;
        }
    }

    public void stop() {
        mediaCodec.stop();
        mediaCodec.release();
        mediaCodec = null;
        inputBuffers = null;
        outputBuffers = null;
    }
}
