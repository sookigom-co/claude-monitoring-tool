#!/usr/bin/env bash
# Builds and runs the host tests; needs `pio run -e smartclock` once so ArduinoJson is in .pio/libdeps.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/smartclock-test-usage.$$"
g++ -std=c++17 -Wall -Wextra -O1 \
  -I "$ROOT/test/pc" -I "$ROOT/include" -I "$ROOT/.pio/libdeps/smartclock/ArduinoJson/src" \
  "$ROOT/test/pc/test_usage.cpp" "$ROOT/src/usage_model.cpp" "$ROOT/src/settings_model.cpp" "$ROOT/src/fonts_gen.cpp" -o "$OUT"
trap 'rm -f "$OUT"' EXIT
"$OUT"
