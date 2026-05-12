#!/usr/bin/env bash
set -euo pipefail

BROWSER_BIN="${BROWSER_BIN:-./build/Release/otf-browser}"

if [[ ! -x "$BROWSER_BIN" ]]; then
  echo "Missing browser binary: $BROWSER_BIN" >&2
  exit 1
fi

"$BROWSER_BIN" --no-sandbox --ozone-platform=x11 &
browser_pid=$!
cleanup() {
  kill "$browser_pid" >/dev/null 2>&1 || true
}
trap cleanup EXIT

window_id="$(xdotool search --sync --pid "$browser_pid" | head -n 1)"
xdotool windowactivate "$window_id"
sleep 1

xdotool key --window "$window_id" ctrl+t
sleep 1
xdotool type --window "$window_id" --delay 20 "example.com"
xdotool key --window "$window_id" Return
sleep 2

xdotool key --window "$window_id" ctrl+w
sleep 1

xdotool key --window "$window_id" ctrl+t
sleep 1
xdotool type --window "$window_id" --delay 20 "browser://settings"
xdotool key --window "$window_id" Return
sleep 2
