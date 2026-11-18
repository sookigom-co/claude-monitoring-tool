#!/usr/bin/env bash
# Renders every screen design on the PC through the real lcd.cpp/ui.cpp and checks redraw behaviour.
# Needs `pio run -e smartclock` once so ArduinoJson is in .pio/libdeps. Usage: run.sh <out-dir>
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
OUT_DIR="${1:?usage: run.sh <out-dir>}"
mkdir -p "$OUT_DIR"
BIN="$OUT_DIR/render_screens"
g++ -std=c++17 -Wall -Wextra -O1 \
  -I "$ROOT/test/pc/sim" -I "$ROOT/include" -I "$ROOT/.pio/libdeps/smartclock/ArduinoJson/src" \
  -Wl,--wrap=time -Wl,--wrap=_ZN3Lcd8drawRingEffffPK7ArcSpanht -Wl,--wrap=_ZN3Lcd7drawBarEsssssttt \
  -Wl,--wrap=_ZN3Lcd8drawTextERK4Fontsss5AlignPKctt \
  "$ROOT/test/pc/sim/render_screens.cpp" "$ROOT/src/lcd.cpp" "$ROOT/src/ui.cpp" "$ROOT/src/usage_model.cpp" \
  "$ROOT/src/settings_model.cpp" "$ROOT/src/i18n.cpp" "$ROOT/src/fonts_gen.cpp" -o "$BIN"
"$BIN" "$OUT_DIR"
