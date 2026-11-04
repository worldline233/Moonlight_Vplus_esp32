#pragma once

#include <cstdint>
#include <functional>

// Value snapshot. Readers can outlive the input facade and run on the decoder
// thread; they capture shared atomic transport state, never SDL router state.
struct Esp32InputStatus
{
    bool enabled = false;
    bool configured = false;
    bool ready = false;
    bool paused = false;
    int rttMs = -1;
    unsigned reconnects = 0;
    bool keyboardUsb = false;
    std::uint8_t mouseButtons = 0;
    bool hasExcludedKeys = false;
};

using Esp32StatusReader = std::function<Esp32InputStatus()>;
