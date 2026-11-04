// Exercise the production FEC aggregation code; no sockets or input are sent.
#include "ControlStream.c"
#include <assert.h>
int main(void) {
    PltCreateMutex(&fecSummaryMutex);
    VideoFecControlSupported = false;
    connectionRecordFecBlock(1, 0, 100, 90);
    assert(fecDataPackets == 0);
    VideoFecControlSupported = true;
    connectionRecordFecBlock(1, 0, 100, 90);
    connectionRecordFecBlock(1, 0, 100, 90);
    assert(fecDataPackets == 100 && fecMissingPackets == 10);
    connectionRecordFecBlock(1, 1, 100, 100);
    connectionRecordFecBlock(2, 0, 0, 0);
    connectionRecordFecBlock(2, 0, 1, 2);
    assert(fecDataPackets == 200 && fecMissingPackets == 10);
    for (unsigned i = 2; i < 20002; ++i) connectionRecordFecBlock(i, 0, 100, 99);
    assert(fecDataPackets == 1000000 && fecMissingPackets == 10008);
    // Summary resets totals, while preserving duplicate suppression across intervals.
    fecDataPackets = fecMissingPackets = 0;
    connectionRecordFecBlock(fecLastFrame, fecLastBlock, 100, 99);
    assert(fecDataPackets == 0);
    connectionRecordFecBlock(fecLastFrame + 1, 0, 100, 100);
    assert(fecDataPackets == 100 && fecMissingPackets == 0);
    PltDeleteMutex(&fecSummaryMutex);
    puts("PASS FEC aggregation: capability gate, clean/lost blocks, deduplication, invalid counts, cap and interval reset");
    return 0;
}
