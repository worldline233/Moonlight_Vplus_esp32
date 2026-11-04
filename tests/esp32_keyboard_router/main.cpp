#include "../../app/streaming/input/esp32keyboardrouter.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

static unsigned checks = 0;
static void check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

int main()
{
    using Router = Esp32KeyboardRouter;
    {
        Esp32LocalShortcutLatch latch;
        check(!latch.consume(66, true), "ordinary function key can follow its normal route");
        latch.latch(66);
        check(latch.consume(66, true), "repeated shortcut down remains local");
        check(!latch.consume(4, true), "shortcut latch does not consume unrelated keys");
        check(latch.consume(66, false), "matching shortcut up remains local");
        check(!latch.consume(66, true), "new press after matching up uses normal route");
        latch.latch(66);
        check(latch.consume(66, true), "fresh down after lost up cannot create unmatched native press");
        check(latch.consume(66, false), "stale shortcut latch clears on physical release");
        check(!latch.consume(66, true), "normal function key resumes after stale latch clears");
        check(!latch.consume(999, true), "unknown scancode cannot index shortcut state");
    }
    {
        Router router;
        auto report = router.handle(224, true, 1);
        check(report.send && report.modifiers == 1, "left control is independent of normal key slots");
        report = router.handle(4, true, 1);
        check(report.send && report.keys[0] == 4 && report.modifiers == 1, "control plus A full state");
        check(!router.handle(4, true, 1).send, "duplicate/repeat down does not replay");
        report = router.handle(4, false, 1);
        check(report.send && report.keys[0] == 0 && report.modifiers == 1, "key up retains modifier");
        check(router.handle(224, false, 1).modifiers == 0, "modifier released");
        check(!router.handle(4, false, 1).send, "orphan up suppressed");
    }
    {
        Router router;
        router.handle(224, true, 1);
        router.handle(4, true, 1);
        router.cancel();
        check(!router.handle(4, true, 1).send, "held key suppressed after local shortcut/focus loss");
        check(!router.handle(224, false, 1).send, "cancelled modifier up suppressed");
        check(!router.handle(4, false, 1).send, "cancelled key up suppressed");
        check(router.handle(4, true, 1).send, "fresh press resumes");
    }
    {
        Router router;
        check(!router.handle(4, true, 0).send, "offline press blocked");
        check(!router.handle(4, true, 1).send, "offline hold is not replayed after connection");
        check(!router.handle(4, false, 1).send, "offline up suppressed after connection");
        check(router.handle(4, true, 1).send, "fresh online press sent");
        check(!router.handle(4, false, 2).send, "old release cannot cross connection epochs");
        auto report = router.handle(5, true, 2);
        check(report.send && report.keys[0] == 5 && report.keys[1] == 0, "no old hold in new connection report");
    }
    {
        Router router;
        for (unsigned key = 4; key < 4 + Router::MaxKeys; ++key) {
            check(router.handle(key, true, 1).send, "sixteen ordinary keys accepted together");
        }
        check(!router.handle(20, true, 1).send, "seventeenth key suppressed");
        for (unsigned key = 224; key < 232; ++key) {
            check(router.handle(key, true, 1).modifiers == (1u << (key - 223)) - 1,
                  "all eight modifiers work alongside sixteen ordinary keys");
        }
        auto report = router.handle(4, false, 1);
        check(report.send && report.modifiers == 255, "release preserves modifiers");
        check(std::find(report.keys.begin(), report.keys.end(), 20) == report.keys.end(),
              "freed slot does not replay a previously suppressed seventeenth key");
        router.handle(20, false, 1);
        check(router.handle(20, true, 1).send, "fresh press can use freed sixteenth slot");
        router.cancel();
        check(!router.handle(5, true, 1).send, "focus loss blocks held replay");
        check(!router.handle(5, false, 2).send, "old hold cannot cross an epoch");
        check(router.handle(5, true, 2).send, "fresh press resumes after reconnect");
    }
    for (unsigned i = 4; i < 100; ++i) {
        if (i == 50) continue; // Firmware does not expose non-US hash.
        check(esp32KeyboardCode(i) != nullptr, "supported HID usage maps to protocol");
    }
    check(std::string(esp32KeyboardCode(39)) == "Digit0", "number-row zero mapping");
    check(std::string(esp32KeyboardCode(98)) == "Numpad0", "numpad zero mapping");
    check(esp32KeyboardCode(232) == nullptr, "unknown modifier rejected");
    for (unsigned seed : {0x32481u, 0x9aa872u}) {
        Router router;
        std::set<unsigned> physical, accepted;
        unsigned random = seed;
        for (unsigned n = 0; n < 30000; ++n) {
            random = random * 1664525u + 1013904223u;
            const unsigned usage = 4 + (random % 26);
            const bool down = (random & 0x10000) != 0;
            if (down) {
                if (physical.insert(usage).second && accepted.size() < Router::MaxKeys) accepted.insert(usage);
            } else {
                physical.erase(usage);
                accepted.erase(usage);
            }
            const auto report = router.handle(usage, down, 1);
            if (!report.send) continue;
            std::set<unsigned> seen;
            for (auto key : report.keys) if (key) {
                check(seen.insert(key).second, "no duplicate usages");
                check(physical.count(key) != 0, "every emitted key is physically held");
            }
            check(seen == accepted, "full report matches independent sixteen-key state model");
        }
    }
    std::cout << "PASS: " << checks << " keyboard routing checks\n";
}
