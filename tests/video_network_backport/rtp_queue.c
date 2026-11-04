// Exercise actual RTP queue loss paths with packet inputs. Transport callbacks are observed locally.
#include "RtpVideoQueue.c"
#include <assert.h>
static bool rfi;
static int lossCount, speculativeCount;
static unsigned lostFrame;
CONNECTION_LISTENER_CALLBACKS ListenerCallbacks;
int AppVersionQuad[4] = {7, 1, 431, -1};
uint32_t EncryptionFeaturesEnabled;
static unsigned long long fakeClock = 1000000000;
bool isReferenceFrameInvalidationEnabled(void) { return rfi; }
void notifyFrameLost(unsigned int frame, bool speculative) { ++lossCount; lostFrame=frame; speculativeCount += speculative; }
void connectionRecordFecBlock(uint32_t frame, uint8_t block, uint16_t data, uint16_t received) { (void)frame;(void)block;(void)data;(void)received; }
void connectionSendFrameFecStatus(PSS_FRAME_FEC_STATUS status) { (void)status; }
void connectionSawFrame(uint32_t frame) { (void)frame; }
void queueRtpPacket(PRTPV_QUEUE_ENTRY packet) { (void)packet; assert(false); }
uint64_t PltGetMicroseconds(void) { return fakeClock; }
static void init(PRTP_VIDEO_QUEUE queue) { memset(queue,0,sizeof(*queue)); queue->currentFrameNumber=10; queue->multiFecCapable=true; lossCount=speculativeCount=0; }
static int packet(PRTP_VIDEO_QUEUE queue, uint32_t frame, unsigned block, unsigned lastBlock) {
    unsigned char bytes[sizeof(RTP_PACKET)+sizeof(NV_VIDEO_PACKET)] = {0};
    PRTP_PACKET rtp=(PRTP_PACKET)bytes;
    PNV_VIDEO_PACKET nv=(PNV_VIDEO_PACKET)(bytes+sizeof(RTP_PACKET));
    RTPV_QUEUE_ENTRY entry={0};
    rtp->sequenceNumber=100;
    nv->frameIndex=LE32(frame);
    nv->multiFecFlags=0x10;
    nv->multiFecBlocks=(block<<4)|(lastBlock<<6);
    nv->fecInfo=LE32(10U<<22);
    return RtpvAddPacket(queue,rtp,sizeof(bytes),&entry);
}
int main(void) {
    RTP_VIDEO_QUEUE queue;
    init(&queue); queue.bufferDataPackets=10;queue.bufferParityPackets=2;queue.missingPackets=3;
    rfi=false;
    assert(reconstructFrame(&queue)==-1 && lossCount==0 && !queue.reportedLostFrame);
    rfi=true;
    assert(reconstructFrame(&queue)==-1 && lossCount==1 && speculativeCount==1 && queue.reportedLostFrame);
    assert(reconstructFrame(&queue)==-1 && lossCount==1);
    // Incoming frame's first block is missing: report its frame index and rearm the next frame.
    init(&queue);
    assert(packet(&queue,12,1,1)==RTPF_RET_REJECTED);
    assert(lossCount==1 && lostFrame==12 && queue.currentFrameNumber==13 && !queue.reportedLostFrame);
    assert(packet(&queue,13,1,1)==RTPF_RET_REJECTED);
    assert(lossCount==2 && lostFrame==13 && queue.currentFrameNumber==14 && !queue.reportedLostFrame);
    // A subsequent block of a partially buffered frame was skipped.
    init(&queue);queue.multiFecLastBlockNumber=2;queue.bufferDataPackets=10;
    RTPV_QUEUE_ENTRY *pending=calloc(1,sizeof(*pending));assert(pending);
    pending->packet=calloc(1,sizeof(RTP_PACKET));assert(pending->packet);
    queue.pendingFecBlockList.head=queue.pendingFecBlockList.tail=pending;queue.pendingFecBlockList.count=1;
    assert(packet(&queue,10,1,2)==RTPF_RET_REJECTED);
    assert(lossCount==1 && lostFrame==10 && queue.currentFrameNumber==11 && !queue.reportedLostFrame);
    assert(queue.pendingFecBlockList.count==0);
    puts("PASS RTP recovery: RFI capability gate, speculative deduplication, skipped block frame index and consecutive losses");
    return 0;
}
