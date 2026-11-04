// Exercise real input enqueue/submission paths with a fake transport and clock.
// No network traffic is generated, and assertions stay enabled in release builds.
#define LC_DEBUG
#define PltGetMillis mouseTestMillis
#include "InputStream.c"
#include <stdarg.h>

int AppVersionQuad[4] = {7, 1, 431, -1};
STREAM_CONFIGURATION StreamConfig;
CONNECTION_LISTENER_CALLBACKS ListenerCallbacks;
static uint64_t testTime;
static int transportResult;
static unsigned terminated;
static unsigned diagnosticLogs;
static char diagnosticLine[1024];

uint64_t mouseTestMillis(void) { return testTime; }

PPLT_CRYPTO_CONTEXT PltCreateCryptoContext(void) {
    return calloc(1, sizeof(PLT_CRYPTO_CONTEXT));
}
void PltDestroyCryptoContext(PPLT_CRYPTO_CONTEXT ctx) { free(ctx); }
bool PltEncryptMessage(PPLT_CRYPTO_CONTEXT ctx, int algorithm, int flags,
                       unsigned char* key, int keyLength, unsigned char* iv, int ivLength,
                       unsigned char* tag, int tagLength, unsigned char* inputData,
                       int inputDataLength, unsigned char* outputData, int* outputDataLength) {
    // Modern encrypted control transport must bypass the legacy cipher path.
    assert(false);
    return false;
}
int sendInputPacketOnControlStream(unsigned char* data, int length, uint8_t channelId,
                                   uint32_t flags, bool moreData) {
    assert(data != NULL && length > 0 && channelId == CTRL_CHANNEL_MOUSE);
    return transportResult;
}
static void testTerminated(int error) {
    assert(error == -1);
    terminated++;
}
static void testLog(const char* format, ...) {
    if (strncmp(format, "MouseDiag transport", 19) != 0) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(diagnosticLine, sizeof(diagnosticLine), format, args);
    va_end(args);
    diagnosticLogs++;
}
static void beginInput(const char* enabled) {
    if (enabled) {
        setenv("MOONLIGHT_MOUSE_DIAGNOSTICS", enabled, 1);
    }
    else {
        unsetenv("MOONLIGHT_MOUSE_DIAGNOSTICS");
    }
    assert(initializeInputStream() == 0);
    initialized = true;
    encryptedControlStream = true;
    transportResult = 0;
    testTime = 0;
    diagnosticLogs = terminated = 0;
}
static void endInput(void) {
    initialized = false;
    LbqSignalQueueShutdown(&packetQueue);
    LbqSignalQueueShutdown(&packetHolderFreeList);
    destroyInputStream();
}
static bool submitNext(void) {
    PPACKET_HOLDER holder;
    assert(LbqPollQueueElement(&packetQueue, (void**)&holder) == LBQ_SUCCESS);
    // These tests isolate enqueue coalescing from the sender's delta extraction.
    if (holder->packet.header.magic == LE32(MOUSE_MOVE_ABS_MAGIC)) {
        currentAbsoluteMouseState.dirty = false;
    }
    else {
        currentRelativeMouseState.dirty = false;
    }
    bool success = sendInputPacket(holder, false);
    freePacketHolder(holder);
    return success;
}
int main(void) {
    ListenerCallbacks.logMessage = testLog;
    ListenerCallbacks.connectionTerminated = testTerminated;

    for (int i = 0; i < 2; ++i) {
        beginInput(i == 0 ? NULL : "0");
        assert(!mouseDiagnosticsEnabled);
        assert(LiSendMouseMoveEvent(1, 1) == 0);
        assert(submitNext());
        assert(mouseDiagnosticRelativeCalls == 0);
        assert(mouseDiagnosticRelativeSubmitted == 0);
        assert(diagnosticLogs == 0);
        endInput();
    }

    beginInput("1");
    assert(mouseDiagnosticsEnabled);
    assert(LiSendMouseMoveEvent(0, 0) == 0);
    assert(LbqGetItemCount(&packetQueue) == 0);
    assert(mouseDiagnosticRelativeCalls == 0);
    for (int i = 0; i < 10; ++i) assert(LiSendMouseMoveEvent(1, 1) == 0);
    assert(LbqGetItemCount(&packetQueue) == 1);
    assert(mouseDiagnosticRelativeCalls == 10);
    assert(submitNext());
    assert(mouseDiagnosticRelativeSubmitted == 1);
    assert(mouseDiagnosticWindowStarted && diagnosticLogs == 0);

    testTime = 1000;
    for (int i = 0; i < 20; ++i) assert(LiSendMouseMoveEvent(1, 1) == 0);
    assert(LbqGetItemCount(&packetQueue) == 1);
    assert(submitNext());
    assert(diagnosticLogs == 1);
    assert(strstr(diagnosticLine, "relative_calls_hz=30.0"));
    assert(strstr(diagnosticLine, "relative_submitted_hz=2.0"));
    assert(mouseDiagnosticRelativeCalls == 0 && mouseDiagnosticRelativeSubmitted == 0);

    testTime = 1500;
    for (int i = 0; i < 3; ++i) assert(LiSendMouseMoveEvent(1, 1) == 0);
    transportResult = -1;
    assert(!submitNext());
    assert(terminated == 1 && mouseDiagnosticRelativeSubmitted == 0);
    transportResult = 0;
    for (int i = 0; i < 5; ++i) assert(LiSendMousePositionEvent(10, 10, 100, 100) == 0);
    assert(LbqGetItemCount(&packetQueue) == 1);
    assert(submitNext());
    assert(diagnosticLogs == 1 && mouseDiagnosticAbsoluteSubmitted == 1);

    testTime = 2000;
    assert(LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT) == 0);
    assert(submitNext());
    assert(diagnosticLogs == 1); // Buttons never inflate motion counts.
    assert(LiSendMousePositionEvent(20, 20, 100, 100) == 0);
    assert(submitNext());
    assert(diagnosticLogs == 2);
    assert(strstr(diagnosticLine, "relative_calls_hz=3.0"));
    assert(strstr(diagnosticLine, "absolute_calls_hz=6.0"));
    assert(strstr(diagnosticLine, "relative_submitted_hz=0.0"));
    assert(strstr(diagnosticLine, "absolute_submitted_hz=2.0"));
    endInput();

    beginInput("1"); // A reconnect starts a new window and clears all counters.
    assert(!mouseDiagnosticWindowStarted && mouseDiagnosticWindowStart == 0);
    assert(mouseDiagnosticRelativeCalls == 0 && mouseDiagnosticAbsoluteCalls == 0);
    assert(mouseDiagnosticRelativeSubmitted == 0 && mouseDiagnosticAbsoluteSubmitted == 0);
    endInput();
    puts("PASS mouse diagnostics: opt-in, motion coalescing, submission success/failure, timing, button exclusion and reconnect reset");
    return 0;
}
