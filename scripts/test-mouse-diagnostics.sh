#!/bin/bash
set -euo pipefail

SOURCE_ROOT=$(cd "$(dirname "$0")/.." && pwd)
COMMON_ROOT="$SOURCE_ROOT/moonlight-common-c/moonlight-common-c"
TEST_ROOT="$SOURCE_ROOT/build/mouse-diagnostics-tests"
mkdir -p "$TEST_ROOT"

# This standalone integration test currently uses the Linux section-GC linker.
# The macOS application's diagnostic paths are built by the macOS workflow.
gcc -std=gnu11 -g -O1 -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -DHAS_SOCKLEN_T -DHAVE_CLOCK_GETTIME=1 \
    -I"$COMMON_ROOT/src" -I"$COMMON_ROOT/enet/include" \
    "$SOURCE_ROOT/tests/mouse_input_diagnostics/main.c" \
    "$COMMON_ROOT/src/Platform.c" "$COMMON_ROOT/src/LinkedBlockingQueue.c" \
    -Wl,--gc-sections -pthread -o "$TEST_ROOT/mouse_input_diagnostics_test"
ASAN_OPTIONS=detect_leaks=0 "$TEST_ROOT/mouse_input_diagnostics_test"
