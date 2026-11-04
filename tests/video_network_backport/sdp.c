// Exercise real SDP generation and stream configuration defaults.
#include "SdpGenerator.c"
#include "Misc.c"
#include <assert.h>
STREAM_CONFIGURATION StreamConfig;
CONNECTION_LISTENER_CALLBACKS ListenerCallbacks;
DECODER_RENDERER_CALLBACKS VideoCallbacks;
AUDIO_RENDERER_CALLBACKS AudioCallbacks;
int AppVersionQuad[4] = {7, 1, 431, -1};
int NegotiatedVideoFormat = VIDEO_FORMAT_H264;
int OriginalVideoBitrate;
uint32_t EncryptionFeaturesEnabled, EncryptionFeaturesSupported, EncryptionFeaturesRequested;
int NegotiatedAudioCodec, NegotiatedAudioBitrate;
bool HighQualitySurroundSupported, ReferenceFrameInvalidationSupported;
bool AudioEncryptionEnabled, HighQualitySurroundEnabled;
int AudioPacketDuration = 5;
uint16_t VideoPortNumber = 47998;
bool VideoFecControlSupported;
uint32_t SunshineFeatureFlags;
uint16_t RtspPortNumber;
char *RemoteAddrString = "127.0.0.1";
struct sockaddr_storage RemoteAddr, LocalAddr;
static char *payload(void) {
    int length;
    char *s=getSdpPayloadForStreamConfig(13,&length);
    assert(s && length>0);
    return s;
}
int main(void) {
    LiInitializeStreamConfiguration(&StreamConfig);
    assert(StreamConfig.videoFecPercentage==-2);
    StreamConfig.width=1920;StreamConfig.height=1080;StreamConfig.fps=60;
    StreamConfig.bitrate=70000;StreamConfig.packetSize=1392;
    StreamConfig.audioConfiguration=AUDIO_CONFIGURATION_STEREO;
    StreamConfig.supportedVideoFormats=VIDEO_FORMAT_H264;
    RemoteAddr.ss_family=LocalAddr.ss_family=AF_INET;
    const int modes[]={-2,-1,0,20,100};
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);++i) {
        StreamConfig.videoFecPercentage=modes[i];
        VideoFecControlSupported=false;
        char *s=payload();assert(!strstr(s,"x-ml-video.fecPercentage"));free(s);
        VideoFecControlSupported=true;
        s=payload();char expected[64];snprintf(expected,sizeof(expected),"x-ml-video.fecPercentage:%d ",modes[i]);
        assert(strstr(s,expected));assert(strstr(s,"x-ml-video.fecSummaryVersion:1 "));free(s);
    }
    puts("PASS FEC SDP: host default, automatic, fixed 0/20/100 and unsupported-host omission");
    return 0;
}
