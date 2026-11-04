// Exercise production partial-IDR state loss and subsequent recovery decisions.
#include "VideoDepacketizer.c"
#include <assert.h>
static int idrRequests;
static unsigned invalidStart, invalidEnd;
void LiRequestIdrFrame(void) { ++idrRequests; }
void connectionDetectedFrameLoss(uint32_t start, uint32_t end) { invalidStart = start; invalidEnd = end; }
CONNECTION_LISTENER_CALLBACKS ListenerCallbacks;
static void resetState(void) {
    nalChainHead = nalChainTail = NULL;
    nalChainDataLength = 0;
    decodingFrame = strictIdrFrameWait = waitingForIdrFrame = waitingForRefInvalFrame = false;
    idrFrameProcessed = true;
    consecutiveFrameDrops = 0;
    startFrameNumber = 10;
    nextFrameNumber = 11;
    invalidStart = invalidEnd = 0;
}
static void partialFrame(int type) {
    LENTRY_INTERNAL *entry = calloc(1, sizeof(*entry));
    assert(entry);
    entry->allocPtr = entry;
    nalChainHead = nalChainTail = &entry->entry;
    frameType = type;
}
int main(void) {
    resetState(); partialFrame(FRAME_TYPE_IDR); dropFrameState();
    assert(waitingForIdrFrame && !waitingForRefInvalFrame && !nalChainHead);
    resetState(); partialFrame(FRAME_TYPE_PFRAME); notifyFrameLost(12, false);
    assert(!waitingForIdrFrame && waitingForRefInvalFrame);
    assert(invalidStart == 10 && invalidEnd == 12 && nextFrameNumber == 13);
    resetState(); strictIdrFrameWait = true; partialFrame(FRAME_TYPE_PFRAME); dropFrameState();
    assert(waitingForIdrFrame && !waitingForRefInvalFrame);
    resetState(); idrFrameProcessed = false; partialFrame(FRAME_TYPE_PFRAME); dropFrameState();
    assert(waitingForIdrFrame);
    resetState(); consecutiveFrameDrops = CONSECUTIVE_DROP_LIMIT - 1; dropFrameState();
    assert(idrRequests == 1 && waitingForIdrFrame && consecutiveFrameDrops == 0);
    puts("PASS recovery: partial IDR, RFI window, disabled RFI, initial IDR and consecutive-drop recovery");
    return 0;
}
