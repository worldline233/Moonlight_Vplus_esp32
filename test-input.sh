#!/bin/bash
set -euo pipefail

SOURCE_ROOT=$(cd "$1" && pwd)
TEST_ROOT="$SOURCE_ROOT/build/cloud-input-tests"
mkdir -p "$TEST_ROOT"

for name in esp32_mouse_router esp32_keyboard_router esp32_performance_stats; do
  c++ -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=undefined -g \
    "$SOURCE_ROOT/tests/$name/main.cpp" -o "$TEST_ROOT/$name"
  "$TEST_ROOT/$name"
done

mkdir -p "$TEST_ROOT/bridge"
cd "$TEST_ROOT/bridge"
qmake "$SOURCE_ROOT/tests/esp32_mouse_bridge/esp32_mouse_bridge.pro" CONFIG+=release
make -j"$(sysctl -n hw.logicalcpu)"
./esp32_mouse_bridge_test
