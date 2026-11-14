#!/usr/bin/env python3
"""Check a full 4MB flash dump: our image at 0x0, 0xFF everywhere else, and no stock-firmware markers."""
import argparse
import hashlib
import sys

FLASH_SIZE = 0x400000
SECTOR = 0x1000
HEADER_PATCHABLE = (2, 3)
# Regions the firmware itself writes once it has booted (eagle.flash.4m1m.ld layout).
RUNTIME_REGIONS = [
    (0x300000, 0x3FA000, "LittleFS (settings.json)"),
    (0x3FB000, 0x3FC000, "EEPROM sector"),
    (0x3FC000, 0x3FD000, "SDK RF calibration"),
    (0x3FD000, 0x400000, "SDK Wi-Fi config"),
]
STOCK_MARKERS = [
    b"SD Pro",
    b"SDPro",
    b"Smart weather clock",
    b"update_ota",
    b"weatherkey",
    b"Filename must start with",
    b"indexwifi.html",
    b"photo-setting.html",
    b"Device Key",
]


def non_ff_sectors(data, start, end):
    ranges = []
    for off in range(start - start % SECTOR, end, SECTOR):
        lo, hi = max(off, start), min(off + SECTOR, end)
        if lo < hi and data[lo:hi].count(0xFF) != hi - lo:
            if ranges and ranges[-1][1] == off:
                ranges[-1][1] = off + SECTOR
            else:
                ranges.append([off, off + SECTOR])
    return ranges


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dump", help="output of: esptool read-flash 0 0x400000 dump.bin")
    ap.add_argument("firmware", help="the firmware.bin that was written at 0x0")
    ap.add_argument("--post-boot", action="store_true",
                    help="dump was taken after the firmware ran; allow its own LittleFS/SDK sectors")
    args = ap.parse_args()

    dump = open(args.dump, "rb").read()
    fw = open(args.firmware, "rb").read()
    failures, notes = [], []

    print(f"dump     {len(dump)} bytes  sha256 {hashlib.sha256(dump).hexdigest()}")
    print(f"firmware {len(fw)} bytes  sha256 {hashlib.sha256(fw).hexdigest()}")
    if len(dump) != FLASH_SIZE:
        failures.append(f"dump size {len(dump)} != {FLASH_SIZE} (read the whole 4MB)")
        dump = dump.ljust(FLASH_SIZE, b"\xff")

    image = bytearray(dump[:len(fw)])
    for i in HEADER_PATCHABLE:
        if image[i] != fw[i]:
            notes.append(f"header byte {i} is 0x{image[i]:02X}, file has 0x{fw[i]:02X} (esptool flash mode/size patch)")
            image[i] = fw[i]
    if bytes(image) != fw:
        first = next(i for i in range(len(fw)) if image[i] != fw[i])
        failures.append(f"image region differs from firmware.bin, first mismatch at 0x{first:06X}")

    allowed = RUNTIME_REGIONS if args.post_boot else []
    for lo, hi in non_ff_sectors(dump, len(fw), FLASH_SIZE):
        inside = [name for a, b, name in allowed if a <= lo and hi <= b]
        if inside:
            notes.append(f"0x{lo:06X}-0x{hi:06X} written at runtime ({inside[0]})")
        else:
            failures.append(f"0x{lo:06X}-0x{hi:06X} is not erased (not 0xFF) and not part of our image")

    for marker in STOCK_MARKERS:
        pos = dump.find(marker)
        if pos >= 0:
            failures.append(f"stock firmware marker {marker!r} found at 0x{pos:06X}")

    for n in notes:
        print("NOTE ", n)
    for f in failures:
        print("FAIL ", f)
    if failures:
        print("RESULT: FAIL - flash is not a clean copy of our image")
        return 1
    print("RESULT: PASS - flash holds only our image" + (" plus its own runtime data" if args.post_boot else " and erased (0xFF) space"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
