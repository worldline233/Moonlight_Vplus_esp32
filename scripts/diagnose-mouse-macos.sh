#!/bin/bash
set -euo pipefail

if [ "$(uname -s)" != Darwin ]; then
  echo "Run this helper on the Mac running Moonlight." >&2
  exit 1
fi

APP_BUNDLE=${1:-/Applications/Moonlight.app}
APP_BINARY="$APP_BUNDLE/Contents/MacOS/Moonlight"
if [ ! -x "$APP_BINARY" ]; then
  echo "Moonlight was not found at: $APP_BINARY" >&2
  echo 'Usage: bash diagnose-mouse-macos.sh "/path/to/Moonlight.app"' >&2
  exit 1
fi

OUTPUT_DIR=$(mktemp -d "$HOME/Desktop/Moonlight-mouse-$(date +%Y%m%d-%H%M%S)-XXXXXX")
LAUNCH_LOG="$OUTPUT_DIR/launch.log"
echo "Results: $OUTPUT_DIR"
echo "Fully quit any previous Moonlight instance before continuing."
echo "Stream the desktop and continuously move the mouse for 15 seconds."
echo "Then disconnect and quit Moonlight to collect the diagnostic log."

APP_EXIT=0
MOONLIGHT_MOUSE_DIAGNOSTICS=1 "$APP_BINARY" >"$LAUNCH_LOG" 2>&1 || APP_EXIT=$?

# Release builds redirect application logs to a file even when stderr is
# redirected. Follow the path printed at startup rather than guessing the
# Qt application-data directory or selecting an older run's log.
APP_LOG=$(sed -n 's/^Redirecting log output to //p' "$LAUNCH_LOG" | tail -n 1)
if [ -z "$APP_LOG" ] || [ ! -f "$APP_LOG" ]; then
  APP_LOG="$LAUNCH_LOG"
fi
if ! grep -F 'MouseDiag' "$APP_LOG" >"$OUTPUT_DIR/mouse-diagnostics.log"; then
  echo "No MouseDiag entries were produced. Use a build containing mouse diagnostics." >&2
  echo "Startup output: $LAUNCH_LOG" >&2
  exit 1
fi
cat "$OUTPUT_DIR/mouse-diagnostics.log"
echo "Saved: $OUTPUT_DIR/mouse-diagnostics.log"
exit "$APP_EXIT"
