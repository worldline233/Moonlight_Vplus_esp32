#pragma once

#include "esp32inputstatus.h"

#include <QString>
#include <cstdint>
#include <functional>
#include <memory>

// SDL-thread facade. All network I/O lives on a separate Qt event-loop thread.
class Esp32MouseBridge
{
public:
    Esp32MouseBridge(const QString& hostName, const QString& hostUuid,
                     const QString& configPath = QString(),
                     std::function<void(const QString&)> notify = {});
    ~Esp32MouseBridge();
    Esp32MouseBridge(const Esp32MouseBridge&) = delete;
    Esp32MouseBridge& operator=(const Esp32MouseBridge&) = delete;

    bool handle(unsigned hidButton, bool down);
    // True consumes the event, including while disconnected in exclusive mode.
    bool handleKeyboard(unsigned scancode, bool down, unsigned physicalScancode = 0);
    bool keyboardExclusive() const;
    bool keyboardExcluded(unsigned scancode) const;
    void releaseKeyboard();
    bool ready() const;
    bool configured() const;
    Esp32InputStatus status() const;
    Esp32StatusReader statusReader() const;
    std::uint8_t setPaused(bool paused);
    std::uint8_t reconnect();
    std::uint8_t releaseAll();

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
