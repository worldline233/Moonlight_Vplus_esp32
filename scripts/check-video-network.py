"""Run Linux ASan/UBSan regressions against the vendored common-c sources."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    project = Path(__file__).resolve().parents[1]
    common = project / "moonlight-common-c/moonlight-common-c"
    build = project / "build/video-network-tests"
    overlay = build / "common-src"
    shutil.copytree(common / "src", overlay, dirs_exist_ok=True)
    environment = dict(os.environ)
    # LeakSanitizer cannot inspect processes inside the WSL agent sandbox.
    # AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.
    environment["ASAN_OPTIONS"] = "detect_leaks=0"
    results = []
    for name in ("summary", "depacketizer", "rtp_queue", "sdp", "capability"):
        command = [
            "gcc", "-std=gnu11", "-g", "-O1", "-ffunction-sections", "-fdata-sections",
            "-fsanitize=undefined,address", "-DHAS_SOCKLEN_T", "-DHAVE_CLOCK_GETTIME=1",
            *["-I" + str(path) for path in (overlay, common / "enet/include", common / "nanors",
                                           common / "nanors/deps", common / "nanors/deps/obl")],
            str(project / f"tests/video_network_backport/{name}.c"),
        ]
        if name in ("summary", "sdp"):
            command.append(str(overlay / "Platform.c"))
        if name == "sdp":
            command.append(str(overlay / "PlatformSockets.c"))
        if name == "rtp_queue":
            command.extend(str(common / relative) for relative in (
                "nanors/rs.c", "nanors/deps/obl/oblas_common.c", "nanors/deps/obl/oblas_lite.c"))
        command.extend(["-Wl,--gc-sections", "-pthread", "-o", str(build / name)])
        subprocess.run(command, check=True)
        result = subprocess.run([str(build / name)], check=True, env=environment,
                                text=True, capture_output=True)
        print(result.stdout, end="", flush=True)
        results.append({"test": name, "status": "passed", "output": result.stdout.strip()})
    (build / "results.json").write_text(json.dumps({
        "status": "passed", "sanitizers": ["address", "undefined"],
        "leak_sanitizer": "disabled_under_sandbox", "tests": results,
        "live_network_or_game_test": "not_run",
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
