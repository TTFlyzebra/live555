#include <jni.h>
#include <string.h>
#include <android/log.h>
#include <sys/socket.h>
#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

#define LOG_TAG "live555Lib"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

JNIEnv *jniEnv;
jobject jobj;

void javaOnResult(const char *result);

void javaOnVideo(const char *videoBytes, int length);

void javaOnAudio(const char *audioBytes, int length);

void javaOnSPS_PPS(const char *sps, int len1, const char *pps, int len2);

void openURL(UsageEnvironment &env, char const *progName, char const *rtspURL);

static bool firstFrame = true;
static bool fHaveWrittenFirstFrame = false;
static char nalu_buffer[1024 * 1024] = {0};    // 用于保存添加起始码等信息的视频数据
static char *p_nalu_pos = nalu_buffer;            // nalu_buffer数组的游标
unsigned char const start_code[4] = {0x00, 0x00, 0x00, 0x01};    // 起始码
unsigned char const connect_0601[4] = {0x03, 0x00, 0x01, 0x80};    // 起始码
static struct timeval pre_time_stamp = {0, 0};

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

#define RTSP_CLIENT_VERBOSITY_LEVEL 1 // by default, print verbose output from each "RTSPClient"

static unsigned rtspClientCount = 0; // Counts how many streams (i.e., "RTSPClient"s) are currently in use.

void openURL(UsageEnvironment &env, char const *progName, char const *rtspURL) {
    RTSPClient *rtspClient = ourRTSPClient::createNew(env, rtspURL, RTSP_CLIENT_VERBOSITY_LEVEL,
                                                      progName);
    if (rtspClient == NULL) {
//        javaOnResult(jniEnv, thiz, "Failed to create a RTSP client for URL \"%s\": %s\n", rtspURL,env.getResultMsg());
        LOGE("Failed to create a RTSP client for URL \"%s\": %s\n", rtspURL, env.getResultMsg());
        javaOnResult("Failed to create a RTSP client for URL");
        return;
    }

    ++rtspClientCount;
    rtspClient->sendDescribeCommand(continueAfterDESCRIBE);
    LOGI("openURL OK! url=%s", rtspURL);
}

void continueAfterDESCRIBE(RTSPClient *rtspClient, int resultCode, char *resultString) {
    do {
        UsageEnvironment &env = rtspClient->envir(); // alias
        StreamClientState &scs = ((ourRTSPClient *) rtspClient)->scs; // alias

        if (resultCode != 0) {
            env << *rtspClient << "Failed to get a SDP description: " << resultString << "\n";
            delete[] resultString;
            break;
        }

        char *const sdpDescription = resultString;
        env << *rtspClient << "Got a SDP description:\n" << sdpDescription << "\n";

        // Create a media session object from this SDP description:
        scs.session = MediaSession::createNew(env, sdpDescription);
        delete[] sdpDescription; // because we don't need it anymore
        if (scs.session == NULL) {
            env << *rtspClient
                << "Failed to create a MediaSession object from the SDP description: "
                << env.getResultMsg() << "\n";
            break;
        } else if (!scs.session->hasSubsessions()) {
            env << *rtspClient << "This session has no media subsessions (i.e., no \"m=\" lines)\n";
            break;
        }

        scs.iter = new MediaSubsessionIterator(*scs.session);
        setupNextSubsession(rtspClient);
        return;
    } while (0);

    shutdownStream(rtspClient);
}

#define REQUEST_STREAMING_OVER_TCP False

void setupNextSubsession(RTSPClient *rtspClient) {
    UsageEnvironment &env = rtspClient->envir(); // alias
    StreamClientState &scs = ((ourRTSPClient *) rtspClient)->scs; // alias

    scs.subsession = scs.iter->next();
    if (scs.subsession != NULL) {
        if (!scs.subsession->initiate()) {
            env << *rtspClient << "Failed to initiate the \"" << *scs.subsession
                << "\" subsession: " << env.getResultMsg() << "\n";
            setupNextSubsession(rtspClient); // give up on this subsession; go to the next one
        } else {
            env << *rtspClient << "Initiated the \"" << *scs.subsession << "\" subsession (";
            if (scs.subsession->rtcpIsMuxed()) {
                env << "client port " << scs.subsession->clientPortNum();
            } else {
                env << "client ports " << scs.subsession->clientPortNum() << "-"
                    << scs.subsession->clientPortNum() + 1;
            }
            env << ")\n";

            // Continue setting up this subsession, by sending a RTSP "SETUP" command:
            rtspClient->sendSetupCommand(*scs.subsession, continueAfterSETUP, False,
                                         REQUEST_STREAMING_OVER_TCP);
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
            env << *rtspClient << "Failed to set up the \"" << *scs.subsession << "\" subsession: "
                << resultString << "\n";
            break;
        }

        env << *rtspClient << "Set up the \"" << *scs.subsession << "\" subsession (";
        if (scs.subsession->rtcpIsMuxed()) {
            env << "client port " << scs.subsession->clientPortNum();
        } else {
            env << "client ports " << scs.subsession->clientPortNum() << "-"
                << scs.subsession->clientPortNum() + 1;
        }
        env << ")\n";

        scs.subsession->sink = DummySink::createNew(env, *scs.subsession, rtspClient->url());
        // perhaps use your own custom "MediaSink" subclass instead
        if (scs.subsession->sink == NULL) {
            env << *rtspClient << "Failed to create a data sink for the \"" << *scs.subsession
                << "\" subsession: " << env.getResultMsg() << "\n";
            break;
        }

        env << *rtspClient << "Created a data sink for the \"" << *scs.subsession
            << "\" subsession\n";
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

    if (--rtspClientCount == 0) {
        exit(exitCode);
    }
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

#define DUMMY_SINK_RECEIVE_BUFFER_SIZE 100000

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


static char *buf = new char[1024 * 1024];
unsigned char const pstart[22] = {0x00, 0x00, 0x00, 0x01, 0x06, 0x01, 0x09, 0x00, 0x0c, 0x08, 0x24,
                                  0x68, 0x00, 0x00, 0x03, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x01};
unsigned char const istart[107] = {0x00, 0x00, 0x00, 0x01, 0x27, 0x4d, 0x40, 0x2a, 0x9a, 0x64, 0x06,
                                   0xc1, 0xed, 0x35, 0x01, 0x01, 0x01, 0x40, 0x00, 0x00, 0xfa, 0x40,
                                   0x00, 0x3a, 0x98, 0x3a, 0x10, 0x00, 0x80, 0x00, 0x00, 0x3f, 0xff,
                                   0x2e, 0xf2, 0xe3, 0x42, 0x00, 0x10, 0x00, 0x00, 0x07, 0xff, 0xe5,
                                   0xde, 0x5c, 0x28, 0x00, 0x00, 0x00, 0x01, 0x28, 0xee, 0x3c, 0x80,
                                   0x00, 0x00, 0x00, 0x01, 0x06, 0x00, 0x0d, 0x80, 0xa9, 0xb0, 0x00,
                                   0x06, 0x18, 0x00, 0xa9, 0xb0, 0x00, 0x06, 0x18, 0x40, 0x80, 0x00,
                                   0x00, 0x00, 0x01, 0x06, 0x01, 0x09, 0x00, 0x10, 0x08, 0x24, 0x68,
                                   0x00, 0x00, 0x03, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x01, 0x06,
                                   0x06, 0x01, 0xc4, 0x80, 0x00, 0x00, 0x00, 0x01};    // 起始码
void DummySink::afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
                                  struct timeval presentationTime,
                                  unsigned /*durationInMicroseconds*/) {
    int type = 0;
    if (firstFrame) {
        unsigned int num;
        SPropRecord *sps = parseSPropParameterSets(fSubsession.fmtp_spropparametersets(), num);
        javaOnSPS_PPS(reinterpret_cast<const char *>(sps[0].sPropBytes), sps[0].sPropLength,
                      reinterpret_cast<const char *>(sps[1].sPropBytes), sps[1].sPropLength);
        delete[] sps;
        firstFrame = False;
    }
    if ((0 == strcmp(fSubsession.codecName(), "H264"))) {
        type = (u_int8_t) (fReceiveBuffer[0] & 0x1f);
        if (5 == type) {
            memcpy(buf, istart, 107);
            memcpy(buf + 107, fReceiveBuffer, frameSize);
            javaOnVideo(buf, frameSize + 107);
        } else if (1 == type) {
            memcpy(buf, pstart, 22);
            memcpy(buf + 22, fReceiveBuffer, frameSize);
            javaOnVideo(buf, frameSize + 22);
        }
    }
//    LOGI("stream ID=%d,mediumName=%s,codecName=%s,size=%d,\n", fStreamId,fSubsession.mediumName(), fSubsession.codecName(), frameSize);
//    if ((0 == strcmp(fSubsession.codecName(),"H264"))) {
//        buf[0] = 0x00;
//        buf[1] = 0x00;
//        buf[2] = 0x00;
//        buf[3] = 0x01;
//        memcpy(buf + 4, fReceiveBuffer, frameSize);
//        javaOnVideo(buf, frameSize);
//    } else {
////        char *buf = new char[frameSize];
////        memcpy(buf, fReceiveBuffer, frameSize);
////        javaOnAudio(buf,frameSize);
////        delete[] buf;
//    }
//
//    int dateLen = 0;
//    u_int8_t nal_unit_type = 0;
//    nal_unit_type = (u_int8_t) (fReceiveBuffer[0] & 0x1f);
//    // 此时,fReceiveBuffer中保存着接收到的视频数据,对该帧数据进行保存
////    LOGI("stream ID=%d,mediumName=%s,codecName=%s,size=%d,\n", fStreamId,fSubsession.mediumName(), fSubsession.codecName(), frameSize);
//    if ((0 == strcmp(fSubsession.codecName(), "H264"))) {
//        // 仅每次播放的第一次进入执行本段代码
//        if (!fHaveWrittenFirstFrame) {    // 对视频数据的SPS,PPS进行补偿
//            if (nal_unit_type != 5) {
//                continuePlaying();
//                return;
//            }
//            unsigned numSPropRecords;
//            SPropRecord *sPropRecords = parseSPropParameterSets(
//                    fSubsession.fmtp_spropparametersets(), numSPropRecords);
//            for (unsigned i = 0; i < numSPropRecords; ++i) {
//                memcpy(p_nalu_pos, start_code, 4);
//                p_nalu_pos += 4;
//                memcpy(p_nalu_pos, sPropRecords[i].sPropBytes, sPropRecords[i].sPropLength);
//                p_nalu_pos += sPropRecords[i].sPropLength;
//            }
//            memcpy(p_nalu_pos, start_code, 4);
//            p_nalu_pos += 4;
//            memcpy(p_nalu_pos, fReceiveBuffer, frameSize);
//            p_nalu_pos += frameSize;
//            dateLen = (int) (p_nalu_pos - nalu_buffer);
//            fHaveWrittenFirstFrame = true; // 标记SPS,PPS已经完成补偿
//            p_nalu_pos = nalu_buffer;
//            LOGI("1111dateLen=%d",dateLen);
//            javaOnVideo(nalu_buffer, dateLen);
//            pre_time_stamp = presentationTime;
//            continuePlaying();
//            return;
//        } else {
//            if (presentationTime.tv_sec == pre_time_stamp.tv_sec &&
//                presentationTime.tv_usec == pre_time_stamp.tv_usec) {
//                memcpy(p_nalu_pos, start_code, 4);
//                p_nalu_pos += 4;
//                memcpy(p_nalu_pos, fReceiveBuffer, frameSize);
//                p_nalu_pos += frameSize;
//                dateLen = p_nalu_pos - nalu_buffer;
//                if (5 == nal_unit_type) {
//                    p_nalu_pos = nalu_buffer;
//                    pre_time_stamp = presentationTime;
//                    LOGI("2222dateLen=%d",dateLen);
//                    javaOnVideo(nalu_buffer, dateLen);
//                    continuePlaying();
//                    return;
//                } else {
//                    pre_time_stamp = presentationTime;
//                    continuePlaying();
//                    return;
//                }
//            } else {
//                p_nalu_pos = nalu_buffer;
//                memcpy(p_nalu_pos, start_code, sizeof(start_code));
//                p_nalu_pos += sizeof(start_code);
//                memcpy(p_nalu_pos, fReceiveBuffer, frameSize);
//                p_nalu_pos += frameSize;
//                dateLen = p_nalu_pos - nalu_buffer;
//                if (p_nalu_pos != nalu_buffer && dateLen > 100) {
//                    LOGI("3333dateLen=%d",dateLen);
//                    javaOnVideo(nalu_buffer, dateLen);
//                    p_nalu_pos = nalu_buffer;
//                }
//            }
//        }
//        pre_time_stamp = presentationTime;
//    }
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
Java_com_flyzebra_live555_rtsp_RtspClient_openUrl(JNIEnv *e, jobject t, jstring jurl) {
    jniEnv = e;
    jobj = t;
    const char *surl;
    surl = jniEnv->GetStringUTFChars(jurl, 0);
    if (surl == NULL) {
        javaOnResult("OutOfMemoryError already thrown");
    }
    LOGI("start open %s", surl);
    openRtspURL(surl);
//    env->ReleaseStringUTFChars(url, str);
}



extern "C" JNIEXPORT void
JNICALL
Java_com_flyzebra_live555_rtsp_RtspClient_stop(JNIEnv *env, jobject thiz) {
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

void javaOnSPS_PPS(const char *sps, int len1, const char *pps, int len2) {
    static jmethodID onAudio = NULL;
    if (onAudio == NULL) {
        jclass cls = (*jniEnv).GetObjectClass(jobj);
        onAudio = (*jniEnv).GetMethodID(cls, "onSPS_PPS", "([B[B)V");
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

