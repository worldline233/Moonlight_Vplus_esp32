#include "../../app/streaming/input/esp32mouserouter.h"

#include <cstdlib>
#include <iostream>

static unsigned checks = 0;
static void check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

int main()
{
    using Router = Esp32MouseRouter;
    {
        Router router;
        auto down = router.handle(0, true, 0, 0x01, true);
        check(down.consume && !down.sendUsb, "paused selected mouse button does not fall back to native");
        check(!router.handle(1, true, 0, 0x01, true).consume, "unselected button retains native route while paused");
        check(router.handle(0, false, 1, 0x01).consume, "paused held release stays suppressed on resume");
        check(router.handle(0, true, 1, 0x01).sendUsb, "fresh click resumes USB");
        check(!router.handle(1, false, 1, 0x01).consume, "unselected native release stays native after resume");
    }
    {
        Router router;
        auto down = router.handle(0, true, 1);
        auto up = router.handle(0, false, 1);
        check(down.consume && down.sendUsb && down.usbButtons == 1, "USB press consumes native send");
        check(up.consume && up.sendUsb && up.usbButtons == 0, "USB release consumes native send");
    }
    {
        Router router;
        check(!router.handle(0, true, 0).consume, "disconnected press is native");
        check(!router.handle(0, false, 0).consume, "disconnected release is native");
        check(router.handle(0, false, 1).consume, "orphan up does not become a native release");
    }
    {
        Router router;
        check(!router.handle(0, true, 0).consume, "native press before connection");
        auto up = router.handle(0, false, 1);
        check(!up.consume && !up.sendUsb, "native hold stays native when connection becomes ready");
        check(router.handle(0, true, 1).sendUsb, "next complete press can use USB");
    }
    {
        Router router;
        router.handle(0, true, 1);
        check(router.handle(0, true, 0).consume, "disconnect while held suppresses repeated down");
        auto up = router.handle(0, false, 2);
        check(up.consume && !up.sendUsb, "old hold never crosses a reconnect");
        check(router.handle(0, true, 2).sendUsb, "fresh press after reconnect uses new epoch");
    }
    {
        Router router;
        router.handle(0, true, 4);
        auto duplicate = router.handle(0, true, 4);
        check(duplicate.consume && !duplicate.sendUsb, "duplicate USB down does not enqueue another report");
        check(router.cancel() == 0, "USB cancel never releases native buttons");
        check(!router.handle(0, true, 4).sendUsb, "cancelled hold is not replayed");
        check(!router.handle(0, false, 4).sendUsb, "cancelled up stays suppressed");
        check(router.handle(0, true, 4).sendUsb, "released and repressed button resumes");
    }
    {
        Router router;
        router.handle(1, true, 0);
        router.handle(4, true, 0);
        check(router.cancel() == 0x12, "native cancel returns precisely the held native buttons");
        check(router.cancel() == 0, "repeated cancel does not duplicate native releases");
        check(router.handle(1, false, 1).consume, "post-cancel up does not reach another route");
    }
    {
        Router router;
        check(router.handle(0, true, 1, 0x01).sendUsb, "selected left button uses USB");
        check(!router.handle(1, true, 1, 0x01).consume, "unselected right button stays native");
        check(router.handle(2, true, 1, 0x05).usbButtons == 0x05, "full USB state retains other selected holds");
        check(router.handle(0, false, 1).usbButtons == 0x04, "releasing left retains middle");
        check(!router.handle(1, false, 2).consume, "native right release survives connection change");
    }
    {
        Router router;
        check(!router.handle(99, true, 1).consume, "unsupported input keeps original routing");
        for (unsigned button = 0; button < 5; ++button) {
            auto down = router.handle(button, true, 1);
            check(down.consume && down.sendUsb && down.usbButtons == (1u << button), "all five HID buttons map independently");
            check(router.handle(button, false, 1).usbButtons == 0, "all five HID buttons release");
        }
    }
    {
        Router router;
        unsigned nativeSends = 0, usbSends = 0;
        for (unsigned i = 0; i < 10000; ++i) {
            for (bool down : {true, false}) {
                const auto decision = router.handle(i % 5, down, 1);
                if (!decision.consume) ++nativeSends;
                if (decision.sendUsb) ++usbSends;
                check(!(decision.sendUsb && !decision.consume), "each event has at most one destination");
            }
        }
        check(nativeSends == 0 && usbSends == 20000, "rapid input keeps every press/release without double routing");
    }
    std::cout << "PASS: " << checks << " router assertions\n";
}
