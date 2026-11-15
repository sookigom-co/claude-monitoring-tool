#!/usr/bin/env bash
# Renders every web page in both languages on the PC through the real web_pages.cpp. Usage: run.sh <out-dir>
# Needs `pio run -e smartclock` once so ArduinoJson is in .pio/libdeps.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
OUT_DIR="${1:?usage: run.sh <out-dir>}"
mkdir -p "$OUT_DIR"
BIN="$OUT_DIR/render_pages"
VERSION="$(sed -n 's/.*-DFW_VERSION=\\"\([^\\]*\)\\".*/\1/p' "$ROOT/platformio.ini")"
g++ -std=c++17 -Wall -Wextra -O1 -DFW_VERSION="\"$VERSION\"" \
  -I "$ROOT/test/pc" -I "$ROOT/include" -I "$ROOT/.pio/libdeps/smartclock/ArduinoJson/src" \
  "$ROOT/test/pc/web/render_pages.cpp" "$ROOT/src/web_pages.cpp" "$ROOT/src/settings_model.cpp" \
  "$ROOT/src/usage_model.cpp" "$ROOT/src/i18n.cpp" -o "$BIN"
"$BIN" "$OUT_DIR"
