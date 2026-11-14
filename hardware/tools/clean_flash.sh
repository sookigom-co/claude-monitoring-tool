#!/usr/bin/env bash
# Erase the whole 4MB flash, write firmware.bin at 0x0, read all 4MB back and verify it is clean.
set -euo pipefail

PORT=${1:?usage: clean_flash.sh <serial-port> [firmware.bin]}
FW=${2:-firmware.bin}
BAUD=${BAUD:-115200}
ESPTOOL=${ESPTOOL:-esptool}
DUMP=${DUMP:-flash-dump-$(date +%Y%m%d-%H%M%S).bin}
HERE=$(cd "$(dirname "$0")" && pwd)

[ -f "$FW" ] || { echo "firmware not found: $FW" >&2; exit 2; }

pause() {
  if [ "${AUTO_RESET:-0}" != "1" ]; then
    read -r -p ">>> Keep GPIO0 on GND, unplug and replug the clock's power, then press Enter. "
  fi
}

run() { "$ESPTOOL" --chip esp8266 --port "$PORT" --baud "$BAUD" --after no-reset "$@"; }

echo "[1/4] chip check"
run chip-id
pause
echo "[2/4] erase the entire flash"
run erase-flash
pause
echo "[3/4] write $FW at 0x0"
run write-flash --flash-mode keep --flash-size keep 0x0 "$FW"
pause
echo "[4/4] read back 4MB to $DUMP"
run read-flash 0x0 0x400000 "$DUMP"

python3 "$HERE/verify_flash.py" "$DUMP" "$FW"
echo "Remove the GPIO0-GND link and power-cycle the clock to boot the new firmware."
