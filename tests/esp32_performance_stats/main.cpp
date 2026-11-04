#include "../../app/streaming/input/esp32performancestats.h"

#include <cstdlib>
#include <iostream>
#include <limits>

static unsigned checks = 0;
static void check(bool result, const char* message)
{
    ++checks;
    if (!result) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
static bool has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

int main()
{
    Esp32InputStatus state;
    auto text = formatEsp32PerformanceStats(state);
    check(text == "ESP32: off\n", "disabled profile stays compact");
    state.enabled = state.configured = state.keyboardUsb = true;
    state.mouseButtons = 31;
    text = formatEsp32PerformanceStats(state);
    check(text == "ESP32: disconnected | Keyboard: blocked | Mouse buttons: Moonlight\n",
          "disconnection shows blocked exclusive keyboard and native mouse fallback");
    state.ready = true;
    state.rttMs = 3;
    text = formatEsp32PerformanceStats(state);
    check(text == "ESP32: connected | Keyboard: USB | Mouse buttons: USB\n",
          "connected routes fit a single line without diagnostic fields");
    state.mouseButtons = 5;
    state.hasExcludedKeys = true;
    text = formatEsp32PerformanceStats(state);
    check(has(text, "Mouse buttons: USB 2/5"), "partial mouse selection remains distinguishable");
    check(!has(text, "excluded"), "exclusion details stay in host settings");
    state.paused = true;
    text = formatEsp32PerformanceStats(state);
    check(text == "ESP32: paused | Keyboard: blocked | Mouse buttons: paused 2/5\n",
          "pause shows blocked USB input rather than claiming native keyboard fallback");
    state.keyboardUsb = false;
    check(has(formatEsp32PerformanceStats(state), "Keyboard: Moonlight"), "native keyboard route remains visible");
    state.configured = false;
    state.ready = false;
    check(has(formatEsp32PerformanceStats(state), "configuration error"), "bad configuration identified");

    const std::string original = "Video stream: 1920x1080\nIncoming frame rate: 120 FPS\nNetwork latency: 4 ms\n";
    const auto combined = appendEsp32PerformanceStats(original, state);
    check(combined.compare(0, original.size(), original) == 0, "existing video/FPS/latency text preserved byte for byte");
    check(appendEsp32PerformanceStats("FPS: 120", state).find("FPS: 120\nESP32:") == 0, "separate statistics line when old text has no trailing newline");
    const auto fullBuffer = appendEsp32PerformanceStats(std::string(1023, 'V'), state);
    check(fullBuffer.substr(0, 1023) == std::string(1023, 'V'), "original full 1024-byte video buffer is not shortened");
    check(fullBuffer.size() < 2048, "ESP32 has separate capacity in enlarged overlay buffer");

    for (unsigned flags = 0; flags < 64; ++flags) {
        state.enabled = flags & 1;
        state.configured = flags & 2;
        state.ready = flags & 4;
        state.paused = flags & 8;
        state.keyboardUsb = flags & 16;
        state.hasExcludedKeys = flags & 32;
        state.rttMs = std::numeric_limits<int>::max();
        state.reconnects = std::numeric_limits<unsigned>::max();
        for (unsigned buttons = 0; buttons < 32; ++buttons) {
            state.mouseButtons = buttons;
            const auto rendered = formatEsp32PerformanceStats(state);
            check(rendered.size() < 512, "all state combinations fit reserved performance overlay capacity");
            for (unsigned char c : rendered) check(c < 128, "text supported by existing performance font");
            check(!has(rendered, "RTT") && !has(rendered, "reconnects"), "diagnostic fields omitted");
            check(rendered.find('\n') == rendered.size() - 1, "exactly one status line");
            if (!state.enabled) check(rendered == "ESP32: off\n", "disabled config overrides stale USB flags");
        }
    }
    std::cout << "PASS: " << checks << " performance overlay checks\n";
}
