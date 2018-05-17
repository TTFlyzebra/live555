#include <jni.h>
#include <string.h>
#include <android/log.h>
#include <sys/socket.h>
#include <ctime>
#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

#define LOG_TAG "c++live555>>>>"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

// by default, print verbose output from each "RTSPClient"
#define RTSP_CLIENT_VERBOSITY_LEVEL 0
// Even though we're not going to be doing anything with the incoming data, we still need to receive it.
// Define the size of the buffer that we'll use:
#define DUMMY_SINK_RECEIVE_BUFFER_SIZE 1024*1024
JNIEnv *jniEnv;
jobject jobj;

static bool firstFrame = true;
static char const H264Header[4] = {0x00, 0x00, 0x00, 0x01};
static char *H264Buf = new char[DUMMY_SINK_RECEIVE_BUFFER_SIZE];
static char *resultMsg = new char[1024];
static RTSPClient *myRtspClient;
// By default, we request that the server stream its data using RTP/UDP.
// If, instead, you want to request that the server stream via RTP-over-TCP, change the following to True:
static bool USE_TCP = false;

void javaOnResult(const char *result);

void javaOnVideo(const char *videoBytes, int length);

void javaOnAudio(const char *audioBytes, int length);

void javaOnRecvRTP(const char *sps, int len1, const char *pps, int len2);

void openURL(UsageEnvironment &env, char const *progName, char const *rtspURL);

void continueAfterDESCRIBE(RTSPClient *rtspClient, int resultCode, char *resultString);

void continueAfterSETUP(RTSPClient *rtspClient, int resultCode, char *resultString);

void continueAfterPLAY(RTSPClient *rtspClient, int resultCode, char *resultString);

void subsessionAfterPlaying(
        void *clientData); // called when a stream's subsession (e.g., audio or video substream) ends
void
subsessionByeHandler(void *clientData); // called when a RTCP "BYE" is received for a subsession
void streamTimerHandler(void *clientData);

void openURL(UsageEnvironment &env, char const *progName, char const *rtspURL);

void setupNextSubsession(RTSPClient *rtspClient);

void shutdownStream(RTSPClient *rtspClient, int exitCode = 1);

UsageEnvironment &operator<<(UsageEnvironment &env, const RTSPClient &rtspClient) {
    return env << "[URL:\"" << rtspClient.url() << "\"]: ";
}

UsageEnvironment &operator<<(UsageEnvironment &env, const MediaSubsession &subsession) {
    return env << subsession.mediumName() << "/" << subsession.codecName();
}

void usage(UsageEnvironment &env, char const *progName) {
    env << "Usage: " << progName << " <rtsp-url-1> ... <rtsp-url-N>\n";
    env << "\t(where each <rtsp-url-i> is a \"rtsp://\" URL)\n";
}

char eventLoopWatchVariable = 0;

int openRtspURL(const char *url) {
    TaskScheduler *scheduler = BasicTaskScheduler::createNew();
    UsageEnvironment *environment = BasicUsageEnvironment::createNew(*scheduler);
    openURL(*environment, (const char *) "longhorn", url);
    environment->taskScheduler().doEventLoop(&eventLoopWatchVariable);
    return 0;
}

class StreamClientState {
public:
    StreamClientState();

    virtual ~StreamClientState();

public:
    MediaSubsessionIterator *iter;
    MediaSession *session;
    MediaSubsession *subsession;
    TaskToken streamTimerTask;
    double duration;
};

class ourRTSPClient : public RTSPClient {
public:
    static ourRTSPClient *createNew(UsageEnvironment &env, char const *rtspURL,
                                    int verbosityLevel = 0,
                                    char const *applicationName = NULL,
                                    portNumBits tunnelOverHTTPPortNum = 0);

protected:
    ourRTSPClient(UsageEnvironment &env, char const *rtspURL,
                  int verbosityLevel, char const *applicationName,
                  portNumBits tunnelOverHTTPPortNum);

    // called only by createNew();
    virtual ~ourRTSPClient();

public:
    StreamClientState scs;
};

class DummySink : public MediaSink {
public:
    static DummySink *createNew(UsageEnvironment &env,
                                MediaSubsession &subsession, // identifies the kind of data that's being received
                                char const *streamId = NULL); // identifies the stream itself (optional)

private:
    DummySink(UsageEnvironment &env, MediaSubsession &subsession, char const *streamId);

    // called only by "createNew()"
    virtual ~DummySink();

    static void afterGettingFrame(void *clientData, unsigned frameSize, unsigned numTruncatedBytes,
                                  struct timeval presentationTime, unsigned durationInMicroseconds);

    void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
                           struct timeval presentationTime, unsigned durationInMicroseconds);

private:
    virtual Boolean continuePlaying();

private:
    u_int8_t *fReceiveBuffer;
    MediaSubsession &fSubsession;
    char *fStreamId;
};

//static unsigned rtspClientCount = 0; // Counts how many streams (i.e., "RTSPClient"s) are currently in use.
void openURL(UsageEnvironment &env, char const *progName, char const *rtspURL) {
    RTSPClient *rtspClient = ourRTSPClient::createNew(env, rtspURL, RTSP_CLIENT_VERBOSITY_LEVEL, progName);
    if (rtspClient == NULL) {
        sprintf(resultMsg, "Failed to create a RTSP client for URL \"%s\": %s\n", rtspURL,
                env.getResultMsg());
        LOGE("%s", resultMsg);
        javaOnResult(resultMsg);
        return;
    }
    myRtspClient = rtspClient;
    rtspClient->sendDescribeCommand(continueAfterDESCRIBE);
    sprintf(resultMsg, "openURL OK! url=%s", rtspURL);
    LOGD("%s", resultMsg);
    return;
}

void continueAfterDESCRIBE(RTSPClient *rtspClient, int resultCode, char *resultString) {
    do {
        UsageEnvironment &env = rtspClient->envir(); // alias
        StreamClientState &scs = ((ourRTSPClient *) rtspClient)->scs; // alias
        if (resultCode != 0) {
            sprintf(resultMsg, "Failed to get a SDP description:\n %s", resultString);
            LOGE("%s", resultMsg);
            delete[] resultString;
            break;
        }
        char *const sdpDescription = resultString;
        LOGD("Got a SDP description:\n %s", sdpDescription);
        scs.session = MediaSession::createNew(env, sdpDescription);
        delete[] sdpDescription; // because we don't need it anymore
        if (scs.session == NULL) {
            LOGD("Failed to create a MediaSession object from the SDP description:  %s",
                 env.getResultMsg());
            break;
        } else if (!scs.session->hasSubsessions()) {
            LOGD("This session has no media subsessions (i.e., no \"m=\" lines)");
            break;
        }
        scs.iter = new MediaSubsessionIterator(*scs.session);
        setupNextSubsession(rtspClient);
        return;
    } while (0);

    shutdownStream(rtspClient);
}
void setupNextSubsession(RTSPClient *rtspClient) {
    UsageEnvironment &env = rtspClient->envir(); // alias
    StreamClientState &scs = ((ourRTSPClient *) rtspClient)->scs; // alias

    scs.subsession = scs.iter->next();
    if (scs.subsession != NULL) {
        if (!scs.subsession->initiate()) {
            LOGD("Failed to initiate the subsession: %s",env.getResultMsg());
            setupNextSubsession(rtspClient); // give up on this subsession; go to the next one
        } else {
            rtspClient->sendSetupCommand(*scs.subsession, continueAfterSETUP, False, USE_TCP);
        }
        return;
    }

    if (scs.session->absStartTime() != NULL) {
        // Special case: The stream is indexed by 'absolute' time, so send an appropriate "PLAY" command:
        rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY, scs.session->absStartTime(),
                                    scs.session->absEndTime());
    } else {
        scs.duration = scs.session->playEndTime() - scs.session->playStartTime();
        rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY);
    }
}

void continueAfterSETUP(RTSPClient *rtspClient, int resultCode, char *resultString) {
    do {
        UsageEnvironment &env = rtspClient->envir(); // alias
        StreamClientState &scs = ((ourRTSPClient *) rtspClient)->scs; // alias

        if (resultCode != 0) {
            LOGD("Failed to set up the subsession: %s",resultString);
            break;
        }

        env << *rtspClient << "Set up the \"" << *scs.subsession << "\" subsession (";
        if (scs.subsession->rtcpIsMuxed()) {
            LOGD("client port=%d",scs.subsession->clientPortNum());
        } else {
            LOGD("client port1=%d,port2=%d",scs.subsession->clientPortNum(),scs.subsession->clientPortNum() + 1);
        }
        scs.subsession->sink = DummySink::createNew(env, *scs.subsession, rtspClient->url());
        // perhaps use your own custom "MediaSink" subclass instead
        if (scs.subsession->sink == NULL) {
            LOGD("Failed to create a data sink for the subsession: %s" ,env.getResultMsg());
            break;
        }

//        env << *rtspClient << "Created a data sink for the \"" << *scs.subsession
//            << "\" subsession\n";
        scs.subsession->miscPtr = rtspClient; // a hack to let subsession handler functions get the "RTSPClient" from the subsession
        scs.subsession->sink->startPlaying(*(scs.subsession->readSource()),
                                           subsessionAfterPlaying, scs.subsession);
        if (scs.subsession->rtcpInstance() != NULL) {
            scs.subsession->rtcpInstance()->setByeHandler(subsessionByeHandler, scs.subsession);
        }
    } while (0);
    delete[] resultString;
    setupNextSubsession(rtspClient);
}

void continueAfterPLAY(RTSPClient *rtspClient, int resultCode, char *resultString) {
    Boolean success = False;
    do {
        UsageEnvironment &env = rtspClient->envir(); // alias
        StreamClientState &scs = ((ourRTSPClient *) rtspClient)->scs; // alias

        if (resultCode != 0) {
            env << *rtspClient << "Failed to start playing session: " << resultString << "\n";
            break;
        }

        if (scs.duration > 0) {
            unsigned const delaySlop = 2; // number of seconds extra to delay, after the stream's expected duration.  (This is optional.)
            scs.duration += delaySlop;
            unsigned uSecsToDelay = (unsigned) (scs.duration * 1000000);
            scs.streamTimerTask = env.taskScheduler().scheduleDelayedTask(uSecsToDelay,
                                                                          (TaskFunc *) streamTimerHandler,
                                                                          rtspClient);
        }

        env << *rtspClient << "Started playing session";
        if (scs.duration > 0) {
            env << " (for up to " << scs.duration << " seconds)";
        }
        env << "...\n";

        success = True;
    } while (0);
    delete[] resultString;

    if (!success) {
        shutdownStream(rtspClient);
    }
}


// Implementation of the other event handlers:

void subsessionAfterPlaying(void *clientData) {
    MediaSubsession *subsession = (MediaSubsession *) clientData;
    RTSPClient *rtspClient = (RTSPClient *) (subsession->miscPtr);

    // Begin by closing this subsession's stream:
    Medium::close(subsession->sink);
    subsession->sink = NULL;

    // Next, check whether *all* subsessions' streams have now been closed:
    MediaSession &session = subsession->parentSession();
    MediaSubsessionIterator iter(session);
    while ((subsession = iter.next()) != NULL) {
        if (subsession->sink != NULL) return; // this subsession is still active
    }

    // All subsessions' streams have now been closed, so shutdown the client:
    shutdownStream(rtspClient);
}

void subsessionByeHandler(void *clientData) {
    MediaSubsession *subsession = (MediaSubsession *) clientData;
    RTSPClient *rtspClient = (RTSPClient *) subsession->miscPtr;
    UsageEnvironment &env = rtspClient->envir(); // alias

    env << *rtspClient << "Received RTCP \"BYE\" on \"" << *subsession << "\" subsession\n";

    subsessionAfterPlaying(subsession);
}

void streamTimerHandler(void *clientData) {
    ourRTSPClient *rtspClient = (ourRTSPClient *) clientData;
    StreamClientState &scs = rtspClient->scs; // alias

    scs.streamTimerTask = NULL;

    shutdownStream(rtspClient);
}

void shutdownStream(RTSPClient *rtspClient, int exitCode) {
    UsageEnvironment &env = rtspClient->envir(); // alias
    StreamClientState &scs = ((ourRTSPClient *) rtspClient)->scs; // alias

    if (scs.session != NULL) {
        Boolean someSubsessionsWereActive = False;
        MediaSubsessionIterator iter(*scs.session);
        MediaSubsession *subsession;

        while ((subsession = iter.next()) != NULL) {
            if (subsession->sink != NULL) {
                Medium::close(subsession->sink);
                subsession->sink = NULL;

                if (subsession->rtcpInstance() != NULL) {
                    subsession->rtcpInstance()->setByeHandler(NULL,
                                                              NULL); // in case the server sends a RTCP "BYE" while handling "TEARDOWN"
                }

                someSubsessionsWereActive = True;
            }
        }

        if (someSubsessionsWereActive) {
            rtspClient->sendTeardownCommand(*scs.session, NULL);
        }
    }

    env << *rtspClient << "Closing the stream.\n";
    Medium::close(rtspClient);
}

ourRTSPClient *ourRTSPClient::createNew(UsageEnvironment &env, char const *rtspURL,
                                        int verbosityLevel, char const *applicationName,
                                        portNumBits tunnelOverHTTPPortNum) {
    return new ourRTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum);
}

ourRTSPClient::ourRTSPClient(UsageEnvironment &env, char const *rtspURL,
                             int verbosityLevel, char const *applicationName,
                             portNumBits tunnelOverHTTPPortNum)
        : RTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum, -1) {
}

ourRTSPClient::~ourRTSPClient() {
}

StreamClientState::StreamClientState()
        : iter(NULL), session(NULL), subsession(NULL), streamTimerTask(NULL), duration(0.0) {
}

StreamClientState::~StreamClientState() {
    delete iter;
    if (session != NULL) {
        // We also need to delete "session", and unschedule "streamTimerTask" (if set)
        UsageEnvironment &env = session->envir(); // alias
        env.taskScheduler().unscheduleDelayedTask(streamTimerTask);
        Medium::close(session);
    }
}

DummySink *
DummySink::createNew(UsageEnvironment &env, MediaSubsession &subsession, char const *streamId) {
    return new DummySink(env, subsession, streamId);
}

DummySink::DummySink(UsageEnvironment &env, MediaSubsession &subsession, char const *streamId)
        : MediaSink(env),
          fSubsession(subsession) {
    fStreamId = strDup(streamId);
    fReceiveBuffer = new u_int8_t[DUMMY_SINK_RECEIVE_BUFFER_SIZE];
}

DummySink::~DummySink() {
    delete[] fReceiveBuffer;
    delete[] fStreamId;
}

void DummySink::afterGettingFrame(void *clientData, unsigned frameSize, unsigned numTruncatedBytes,
                                  struct timeval presentationTime,
                                  unsigned durationInMicroseconds) {
    DummySink *sink = (DummySink *) clientData;
    sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
}

void DummySink::afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
                                  struct timeval presentationTime,
                                  unsigned /*durationInMicroseconds*/) {
    sprintf(resultMsg, "%10s %10d (bytes)%10ld s%10ld us", fSubsession.codecName(), frameSize,
            presentationTime.tv_sec, presentationTime.tv_usec);
    LOGD("recv %s", resultMsg);
    if (firstFrame) {
        unsigned int num = 0;
        SPropRecord *sPropRecord = parseSPropParameterSets(fSubsession.fmtp_spropparametersets(), num);
        if (num > 1) {
            LOGD("GET SPS-PPS Sucesse!", sPropRecord[0].sPropBytes, sPropRecord[1].sPropBytes);
            javaOnRecvRTP((char *) (sPropRecord[0].sPropBytes), sPropRecord[0].sPropLength,
                          (char *) (sPropRecord[1].sPropBytes), sPropRecord[1].sPropLength);
        } else {
            LOGD("GET SPS-PPS Failed!");
            continuePlaying();
            return;
        }
        firstFrame = False;
    }
    if ((0 == strcmp(fSubsession.codecName(), "H264")) && frameSize > 0) {
        memcpy(H264Buf, H264Header, 4);
        memcpy(H264Buf + 4, fReceiveBuffer, frameSize);
        javaOnVideo(H264Buf, (4 + frameSize));
    }
    continuePlaying();
}

Boolean DummySink::continuePlaying() {
    if (fSource == NULL) return False; // sanity check (should not happen)
    // Request the next frame of data from our input source.  "afterGettingFrame()" will get called later, when it arrives:
    fSource->getNextFrame(fReceiveBuffer, DUMMY_SINK_RECEIVE_BUFFER_SIZE, afterGettingFrame, this,
                          onSourceClosure, this);
    return True;
}

extern "C" JNIEXPORT void
JNICALL
Java_com_flyzebra_live555_rtsp_RtspClient_openUrl(JNIEnv *env, jobject t, jstring jurl) {
    jniEnv = env;
    jobj = t;
    const char *surl;
    surl = env->GetStringUTFChars(jurl, 0);
    if (surl == NULL) {
        javaOnResult("OutOfMemoryError already thrown");
    }
    LOGI("start open %s", surl);
    firstFrame = true;
    openRtspURL(surl);
    (*env).DeleteLocalRef((jobject) surl);
//    e->ReleaseStringUTFChars(jurl, surl);
}



extern "C" JNIEXPORT void
JNICALL
Java_com_flyzebra_live555_rtsp_RtspClient_stop(JNIEnv *env, jobject thiz) {
    shutdownStream(myRtspClient, 0);
}

extern "C" JNIEXPORT void
JNICALL
Java_com_flyzebra_live555_rtsp_RtspClient_useTCP(JNIEnv *env, jobject thiz) {
    USE_TCP = true;
}

/**
 * 在JAV中回调的方法
 * @param env
 * @param thiz
 * @param result
 */
void javaOnResult(const char *result) {
    static jmethodID onresult = NULL;
    if (onresult == NULL) {
        jclass cls = (*jniEnv).GetObjectClass(jobj);
        onresult = (*jniEnv).GetMethodID(cls, "onResult", "(Ljava/lang/String;)V");
        if (onresult == NULL) {
            LOGI("onResult--method not found");
            return; /* method not found */
        }
    }
    jstring jstr = (*jniEnv).NewStringUTF(result);
    (*jniEnv).CallVoidMethod(jobj, onresult, jstr);
    (*jniEnv).DeleteLocalRef(jstr);
}

void javaOnVideo(const char *videoBytes, int length) {
    static jmethodID onVideo = NULL;
    if (onVideo == NULL) {
        jclass cls = (*jniEnv).GetObjectClass(jobj);
        onVideo = (*jniEnv).GetMethodID(cls, "onVideo", "([B)V");
        if (onVideo == NULL) {
            LOGI("onVideo--method not found");
            return; /* method not found */
        }
    }
    jbyteArray jbytes = (*jniEnv).NewByteArray(static_cast<jsize>(length));
    (*jniEnv).SetByteArrayRegion(jbytes, 0, length, reinterpret_cast<const jbyte *>(videoBytes));
    (*jniEnv).CallVoidMethod(jobj, onVideo, jbytes);
    (*jniEnv).DeleteLocalRef(jbytes);
}

void javaOnAudio(const char *audioBytes, int length) {
    static jmethodID onAudio = NULL;
    if (onAudio == NULL) {
        jclass cls = (*jniEnv).GetObjectClass(jobj);
        onAudio = (*jniEnv).GetMethodID(cls, "onAudio", "([B)V");
        if (onAudio == NULL) {
            LOGI("onAudio--method not found");
            return; /* method not found */
        }
    }
    jbyteArray jbytes = (*jniEnv).NewByteArray(static_cast<jsize>(length));
    (*jniEnv).SetByteArrayRegion(jbytes, 0, length, reinterpret_cast<const jbyte *>(audioBytes));
    (*jniEnv).CallVoidMethod(jobj, onAudio, jbytes);
    (*jniEnv).DeleteLocalRef(jbytes);
}

void javaOnRecvRTP(const char *sps, int len1, const char *pps, int len2) {
    static jmethodID onAudio = NULL;
    if (onAudio == NULL) {
        jclass cls = (*jniEnv).GetObjectClass(jobj);
        onAudio = (*jniEnv).GetMethodID(cls, "onRecvRTP", "([B[B)V");
        if (onAudio == NULL) {
            LOGI("onAudio--method not found");
            return; /* method not found */
        }
    }
    jbyteArray jbytes1 = (*jniEnv).NewByteArray(static_cast<jsize>(len1));
    (*jniEnv).SetByteArrayRegion(jbytes1, 0, len1, reinterpret_cast<const jbyte *>(sps));
    jbyteArray jbytes2 = (*jniEnv).NewByteArray(static_cast<jsize>(len2));
    (*jniEnv).SetByteArrayRegion(jbytes2, 0, len2, reinterpret_cast<const jbyte *>(pps));
    (*jniEnv).CallVoidMethod(jobj, onAudio, jbytes1, jbytes2);
    (*jniEnv).DeleteLocalRef(jbytes1);
    (*jniEnv).DeleteLocalRef(jbytes2);
}

