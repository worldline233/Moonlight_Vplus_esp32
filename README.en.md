# Moonlight V+ ESP32 v1.0

A complete source fork of Moonlight V+ 6.4.1 with per-host ESP32 USB keyboard and mouse-button routing. The application and package version is `1.0.0`.

- Protocol 2, 16 ordinary keys plus 8 independent modifiers.
- Per-host settings, excluded keys, local shortcuts, and ESP32 status in the original performance overlay.
- Mouse movement and scrolling continue to use Moonlight.
- Selective FEC negotiation/statistics and video recovery backports.
- The macOS bundle opts out of the SDK 26+ display-synchronized input event loop through `NSApplicationUpdateCycleEnabled=false`, following upstream Moonlight commit `f018a71a8a2a63377acde15366625e8effeae05b`.

Clone with `--recurse-submodules`. The modified common-c library and its enet/nanors dependencies are vendored. Unmodified SDL controller mappings, qmdnsengine, and AMF remain pinned submodules. See [SOURCE-PROVENANCE.json](SOURCE-PROVENANCE.json).

GitHub Actions provides manual **Build macOS v1.0** (arm64 or x86_64) and **Build Windows v1.0** (x64) workflows. They compile this repository directly and upload artifacts for seven days. They do not publish releases automatically.

Local builds require Qt 6.11.1 with Multimedia, Image Formats and WebSockets, Python 3, and the platform toolchain. macOS uses Xcode and `bash scripts/generate-dmg.sh Release`; Windows uses MSVC 2022 with ATL and `scripts/build-arch.bat Release x64`. Set `CI_VERSION=1.0.0` and generate Chinese translations before building. Full commands and ESP32 instructions are in [README.md](README.md).

This fork checks its own GitHub Releases for updates. GPLv3; dependencies retain their individual licenses. Upstream: [qiin2333/moonlight-qt](https://github.com/qiin2333/moonlight-qt).
