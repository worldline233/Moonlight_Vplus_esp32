// Exercise the real host capability gate, without opening a connection.
#include "RtspConnection.c"
#include <assert.h>
int AppVersionQuad[4] = {7, 1, 431, -1};
int main(void) {
    assert(!supportsVideoFecControl("v=0\r\n"));
    assert(!supportsVideoFecControl("a=x-ss-video[0].fecControlVersion:0\r\n"));
    assert(!supportsVideoFecControl("a=x-ss-video[0].fecControlVersion:2\r\n"));
    assert(supportsVideoFecControl("a=x-ss-video[0].fecControlVersion:1\r\n"));
    AppVersionQuad[3]=0;
    assert(!supportsVideoFecControl("a=x-ss-video[0].fecControlVersion:1\r\n"));
    puts("PASS FEC negotiation: absent, unsupported version, compatible Sunshine and non-Sunshine hosts");
    return 0;
}
