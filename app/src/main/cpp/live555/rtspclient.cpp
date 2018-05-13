#include <jni.h>
#include <string.h>
#include <android/log.h>
#include "UsageEnvironment/include/UsageEnvironment.hh"
#include "BasicUsageEnvironment/include/BasicUsageEnvironment.hh"

#define LOG_TAG "live555Lib"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
/**
 * 在JAV中回调的方法
 * @param env
 * @param thiz
 * @param result
 */
void javaResult(JNIEnv* env, jobject thiz,char* result) {
    static jmethodID onresult = NULL;
    jclass cls = (*env).GetObjectClass(thiz);
    if (onresult == NULL) {
        onresult = (*env).GetMethodID(cls, "onResult", "(Ljava/lang/String;)V");
        if (onresult == NULL) {
            return; /* method not found */
        }
    }
    jstring jstr = (*env).NewStringUTF( result);
    (*env).CallVoidMethod(thiz, onresult, jstr);

}

void javaOnVideo(JNIEnv* env, jobject thiz,char* videoBytes) {
    static jmethodID onVideo = NULL;
    jclass cls = (*env).GetObjectClass(thiz);
    if (onVideo == NULL) {
        onVideo = (*env).GetMethodID(cls, "onVideo", "([B)V");
        if (onVideo == NULL) {
            LOGI("onVideo--method not found");
            return; /* method not found */
        }
    }
    jsize len = static_cast<jsize>(strlen(videoBytes));
    jbyteArray jbytes = (*env).NewByteArray(static_cast<jsize>(len));
    (*env).SetByteArrayRegion(jbytes, 0, len, reinterpret_cast<const jbyte *>(videoBytes));
    (*env).CallVoidMethod(thiz, onVideo, jbytes);

}

void javaOnAudio(JNIEnv* env, jobject thiz,char* videoBytes) {
    static jmethodID onAudio = NULL;
    jclass cls = (*env).GetObjectClass(thiz);
    if (onAudio == NULL) {
        onAudio = (*env).GetMethodID(cls, "onAudio", "([B)V");
        if (onAudio == NULL) {
            LOGI("onAudio--method not found");
            return; /* method not found */
        }
    }
    int len = static_cast<int>(strlen(videoBytes));
    jbyteArray jbytes = (*env).NewByteArray(static_cast<jsize>(len));
    (*env).SetByteArrayRegion(jbytes, 0, len, reinterpret_cast<const jbyte *>(videoBytes));
    (*env).CallVoidMethod(thiz, onAudio, jbytes);

}

extern "C" JNIEXPORT void
JNICALL
Java_com_flyzebra_live555_rtsp_RtspClient_openUrl(JNIEnv *env, jobject thiz, jstring url) {
    const char* str;
    str = env->GetStringUTFChars(url, false);
    if(str == NULL) {
        /* OutOfMemoryError already thrown */
    }
    env->ReleaseStringUTFChars(url, str);

    TaskScheduler* scheduler = BasicTaskScheduler::createNew();
    UsageEnvironment* environment = BasicUsageEnvironment::createNew(*scheduler);
    LOGI("connect rtsp %s",str);
}



extern "C" JNIEXPORT void
JNICALL
Java_com_flyzebra_live555_rtsp_RtspClient_stop(JNIEnv *env, jobject thiz) {

}

