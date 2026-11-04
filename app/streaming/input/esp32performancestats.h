#pragma once

#include "esp32inputstatus.h"
#include <string>

// ASCII matches the existing performance overlay and its bundled bitmap-style
// font. No address, access key or typed input belongs in these statistics.
inline std::string formatEsp32PerformanceStats(const Esp32InputStatus& state)
{
    if (!state.enabled) return "ESP32: off\n";
    const std::string connection = !state.configured ? "configuration error" :
                                   state.paused ? "paused" :
                                   state.ready ? "connected" : "disconnected";
    const std::string keyboard = !state.keyboardUsb ? "Moonlight" :
                                 state.paused || !state.ready ? "blocked" : "USB";
    unsigned buttons = 0;
    for (unsigned i = 0; i < 5; ++i) if (state.mouseButtons & (1u << i)) ++buttons;
    std::string mouse = "Moonlight";
    if (buttons && (state.paused || state.ready)) {
        mouse = state.paused ? "paused" : "USB";
        if (buttons < 5) mouse += " " + std::to_string(buttons) + "/5";
    }
    return "ESP32: " + connection + " | Keyboard: " + keyboard +
           " | Mouse buttons: " + mouse + "\n";
}

inline std::string appendEsp32PerformanceStats(std::string videoStats, const Esp32InputStatus& state)
{
    if (!videoStats.empty() && videoStats.back() != '\n') videoStats += '\n';
    return videoStats + formatEsp32PerformanceStats(state);
}
