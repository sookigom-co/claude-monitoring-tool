**English** | [한국어](README.ko.md)

# SmartClock firmware (hardware) — 0.3.0

Custom firmware for the SD Pro smart clock (ESP-12F / ESP8266, 4MB flash, ST7789 240×240 LCD). It was written from scratch and uses no code, fonts, or images from the stock firmware.

- Screen: time (`HH:MM`), date (`Sun, Nov 15` in English, `11월 15일 일요일` in Korean), and two circular gauges for Claude and Fable weekly usage (NTP, default time zone `KST-9`)
- Display language: English or Korean (default Korean) for both the settings web page and the clock screen, with a short explanation under every setting (Section 8.3)
- Three screen designs (A concentric rings / B clock on top + gauges below / C left and right semicircular rings; default C). Fix one design or cycle through them automatically (Section 8.2)
- Usage: draws the JSON provided by the relay server `server/`
- Over-the-air update (OTA): web upload `/update` (Digest authentication + CSRF token) + ArduinoOTA (password required)
- Settings web page: language, usage URL and token, HTTPS certificate check mode, time zone, brightness, screen design, admin password, Wi-Fi reset, factory reset
- HTTPS certificate check: by default, verified against built-in root certificates (ISRG Root X1 and X2 = Let's Encrypt)

Installing the relay server and exposing it publicly (nginx) is covered in [`../server/README.md`](../server/README.md).

## 0. Versions

> **CAUTION** — Version 0.1.1 is used as the version string by two different builds. 0.1.2 lacks the LCD alignment fix, so **on the real device it reboots repeatedly with `Exception (9)` right after boot** (confirmed from 0.1.0 device logs; 0.1.2 has the same code). Do not use 0.1.2; use 0.3.0 (or 0.1.3). The procedure for upgrading from 0.2.0 to 0.3.0 is in Section 9.3, and from 0.1.3 or 0.1.1 (LCD fix build) in Section 9.4.

> **NOTE** — 0.2.0 and 0.3.0 have not yet been checked on the real device. Items marked `UNVERIFIED` in the Section 1 table will be checked after flashing the clock.

## 1. Verification status

| Item | Status |
|---|---|
| `pio run -e smartclock` build (0.3.0) | `CONFIRMED` — RAM 41.1% (33,680 B), Flash 60.3% (629,651 B), `firmware.bin` 633,808 B. Compared with 0.2.0: RAM −816 B, `firmware.bin` +10,832 B |
| `pio run -e lcd-diag` build (0.3.0) | `CONFIRMED` — RAM 35.0% (28,640 B), Flash 31.4% (327,835 B), `firmware.bin` 331,984 B (same as 0.2.0) |
| PC tests (`test/pc/run.sh`) | `CONFIRMED` — 294 checks in 15 groups pass. JSON parsing, status text in both languages, screen layout widths in both languages, settings save and migration (design and language keys), check mode and token warning decisions |
| Rendering and partial updates of the three screens (`test/pc/sim/run.sh`) | `CONFIRMED` (PC) — the SPI bytes actually sent by `lcd.cpp` and `ui.cpp` are received and drawn by a fake ST7789. 2 languages × 3 designs × 10 states = 60 images plus 6 setup/message screens, with 0 pixels of overlap between text boxes and rings. 590 frames within the same minute transfer 0 px; a minute change transfers only the 6,450 px time box; a design or language change triggers one full redraw |
| Web pages (`test/pc/web/run.sh`) | `CONFIRMED` (PC) — the real `web_pages.cpp` writes the admin page, setup portal, update page and message pages in both languages as HTML files. Every text entry exists in both languages, input is HTML-escaped, the largest chunk is under 1.1 KB |
| Auto-cycle timing (`include/layout_cycle.h`) | `CONFIRMED` (PC) — order A→B→C, interval, restart from the beginning on save, `millis()` 32-bit wraparound |
| Settings migration (0.1.x / 0.2.0 → 0.3.0) | `CONFIRMED` (PC) — when 0.3.0 reads a 0.2.0-format `settings.json`, existing values are kept and the language starts as Korean (`ko`). A missing or invalid `lang` value also gives Korean. 0.1.x files additionally get the design defaults (`C`, `fixed`, 15 seconds) |
| LCD pin defaults (Section 3) | `CONFIRMED` — screen output on the board's real device (LCD fix build 0.1.1) on 2026-11-14 |
| Serial flashing procedure (CP2102, macOS) | `CONFIRMED` — the board flashed using the same procedure on 2026-11-14 |
| Built-in root certificates | `CONFIRMED` — `tools/gen_ca_roots.py` checks the SHA-256 fingerprints against fixed values. ISRG Root X1 `96bcec06…bddf08c6`, X2 `69729b8e…3ccb1470` |
| 0.3.0 screen on the real device, designs B and C in Korean | `CONFIRMED` — photos in Section 8.2 (2026-11-16) |
| 0.2.0/0.3.0 screen on the real device: design A, English text, arc drawing time | `UNVERIFIED` — requires the real device. One design change computes roughly 20,000–30,000 ring pixels in floating point |
| Chunked web page delivery on the real device | `UNVERIFIED` — requires the real device. Checked on the PC only (Section 8.3) |
| Wi-Fi setup portal, NTP, usage fetch, both OTA methods | `UNVERIFIED` — no real-device results yet |
| Connecting to a real Let's Encrypt server (nginx) with CA check | `UNVERIFIED` — requires a real domain |
| Free heap during HTTPS connections | `UNVERIFIED` — check with the `tls` value in `/api/status` (Section 8.1) |

## 2. Quick start

| Task | Where to read |
|---|---|
| First flash on a stock clock | Section 4 → Section 5 → Section 6 → Section 7 |
| Upgrade a clock running 0.2.0 to 0.3.0 | Section 9.3 |
| Upgrade a clock running 0.1.3 / 0.1.1 to 0.3.0 | Section 9.4 |
| Change the screen design or set up auto-cycle | Section 8.2 |
| Switch the language to English or Korean | Section 8.3 |
| Connect the usage display | `server/README.md` Section 6 (clock settings) |
| Screen shows nothing | Section 3.1 |
| Build from source yourself | Section 2.1 |

Prebuilt files are in GitHub Releases under `hardware-v0.3.0`. The previous builds remain under `hardware-v0.2.0` and `hardware-v0.1.3`.

| File | Purpose |
|---|---|
| `smartclock-0.3.0-firmware.bin` | Main firmware |
| `lcd-diag-0.3.0-firmware.bin` | Screen pin diagnostics (no Wi-Fi) |
| `SHA256SUMS` | For verifying files |

```bash
sha256sum -c --ignore-missing SHA256SUMS      # macOS: shasum -a 256 -c --ignore-missing SHA256SUMS
```

### 2.1 Building from source

```bash
cd hardware
python3 -m pip install platformio "esptool>=5"
pio run -e smartclock             # → .pio/build/smartclock/firmware.bin
pio run -e lcd-diag               # → .pio/build/lcd-diag/firmware.bin
test/pc/run.sh                    # PC tests. Requires g++. Run after building smartclock once
test/pc/sim/run.sh out/           # PC render of the three screens in both languages + partial update checks → out/*.ppm
python3 test/pc/sim/make_sheet.py out/ png/   # ppm → png and comparison sheets (requires Pillow)
test/pc/web/run.sh web/           # PC render of every web page in both languages → web/*.html
python3 tools/gen_fonts.py        # Only when changing on-screen characters (output src/fonts_gen.cpp is already included)
python3 tools/gen_ca_roots.py     # Only when changing root certificates (output src/ca_roots_gen.cpp is already included)
```

- Platform `espressif8266@4.2.1` (Arduino core 3.1.2), board `esp12e`, 4MB flash in **DOUT mode**, CPU 160MHz.
- Flash layout `eagle.flash.4m1m.ld`: firmware about 1MB, OTA staging area about 2MB, LittleFS 1MB (`0x300000`–`0x3FA000`).
- The only external library is ArduinoJson 7.4.2.
- The screen font is a subset of Noto Sans CJK KR (SIL OFL 1.1) containing only the needed characters. It contains ASCII plus the Hangul in `tools/glyphs_ko.txt`; missing characters are shown as `?`.
- The first build takes several minutes because it downloads the toolchain.

## 3. LCD pin settings

All pin and panel options are on **a single line**, `[lcd] build_flags`, in `platformio.ini`. Change the values and rebuild.

| Flag | Default | Meaning |
|---|---|---|
| `LCD_MOSI` / `LCD_SCLK` | 13 / 14 | SPI data and clock. 13/14 uses hardware SPI; any other value uses software SPI (slow) |
| `LCD_CS` | -1 | Chip select. -1 = not used |
| `LCD_DC` | 0 | Data/command select |
| `LCD_RST` | 2 | Reset. -1 = not used |
| `LCD_BL` | 5 | Backlight |
| `LCD_BL_ACTIVE_LOW` | 1 | 1 = on when LOW |
| `LCD_SPI_MODE` | 3 | Usually 3 for an ST7789 without CS |
| `LCD_INVERT` | 1 | IPS panel color inversion |
| `LCD_BGR` | 0 | Set to 1 if red and blue appear swapped |
| `LCD_ROTATION` | 0 | 0–3 (in 90° steps) |

The defaults are the values that produced a picture on the board's real device on 2026-11-14. Use Section 3.1 to find pins only when using a device from a different product line.

### 3.1 When the screen shows nothing — pin diagnostic build (`lcd-diag`)

1. Flash `lcd-diag-0.3.0-firmware.bin` using the Section 6 procedure.
2. The diagnostic firmware runs through 16 pin candidate combinations × 2 SPI modes × 2 backlight polarities = **64 steps**, 4 seconds each (one full pass takes about 4 minutes 16 seconds).
3. Write down the large number and the two lines below it (`DC0 RST2 CS-1 BL5`, `MODE3 BL-LOW RED`) for the step where the screen is visible. The same information is also printed on serial (115200bps).
4. If the background color differs from the text (`RED`/`GREEN`/`BLUE`), invert `LCD_BGR`.
5. Put the recorded values into `[lcd] build_flags` and rebuild `smartclock`.
6. If nothing is visible in any of the 64 steps, take photos (front and back) of the board-side connector where the screen cable (FPC) plugs in and the surrounding wiring, and attach them to the task.

Viewing the serial log:
```bash
python3 -m serial.tools.miniterm <port> 115200 --dtr 0 --rts 0
```
One line of garbled characters right after boot is normal (text printed by the ESP8266 boot ROM at 74880bps).

## 4. Requirements and wiring

### 4.1 USB-serial adapter

| Product | Verdict |
|---|---|
| **CP2102** | Recommended. Its signal lines are always 3.3V, so it is safe regardless of jumper position (when VCC is not connected) |
| CH340G | Usable. A single jumper changes the signal voltage as well, so **set it to 3.3V** |
| USBasp (HW-437) | **Not usable.** It is an AVR-only ISP programmer and does not provide a serial port |

> **WARNING** — The ESP8266 pins are 3.3V (3.6V max). A 5V signal on the RX pin can damage the chip. Before connecting, plug only the adapter into the PC and measure **TX ↔ GND** with a multimeter. If it reads 3.0–3.4V, connect it. If it reads 4.5V or more, change the jumper and measure again.

### 4.2 Wiring

| Adapter | ESP-12F pad |
|---|---|
| TX | RXD (GPIO3) |
| RX | TXD (GPIO1) |
| GND | GND |
| VCC | **Do not connect.** Power the clock through its USB-C port |
| — | **GPIO0 → GND** jumper: enters download mode. Keep it attached from erase through verification |

- Always tie the adapter GND and the clock GND together.
- Powering on with GPIO0 tied to GND boots into download mode. In this state no firmware runs.
- Wiring locations on the clock board (front, USB-C at the bottom). Labels in the photo are in Korean: red = adapter RX/TX, black = GND, blue = GPIO0 (IO0) jumper to GND, orange = 3V3 (do not connect when the clock is powered over USB-C).

![Wiring locations on the SD Pro clock board](docs/wiring.png)

| Pad in the photo | Position (bottom row, USB-C at the bottom) | Connect to |
|---|---|---|
| TX | rightmost | adapter RX |
| RX | 2nd from right | adapter TX |
| GND | leftmost | adapter GND |
| IO0 | 4th from left | jumper to GND (keep attached while flashing) |

- Before soldering, check continuity with a multimeter: GND pad ↔ USB-C metal shell, and 3V3 pad (top row, leftmost) ↔ regulator heat sink. If either fails, do not solder.

## 5. Installing the flashing tool (esptool)

1. Install Python 3 (only if it is not installed). On Windows, check "Add python.exe to PATH" in the installer.
2. Install esptool and the serial monitor.
   ```bash
   python3 -m pip install "esptool>=5" pyserial
   ```
3. On Windows, if no port appears, install the Silicon Labs "CP210x VCP Driver". macOS and Linux usually need no driver.
4. Plug in the adapter and find the port name.

| OS | How to check | Example |
|---|---|---|
| Windows | Device Manager → Ports (COM & LPT) | `COM3` |
| macOS | `ls /dev/cu.*` | `/dev/cu.usbserial-0001`, `/dev/cu.SLAB_USBtoUART` |
| Linux | `ls /dev/ttyUSB*` | `/dev/ttyUSB0` (on a permission error, run `sudo usermod -aG dialout $USER` and log in again) |

5. Check the connection. With GPIO0 on GND, unplug and replug the clock's USB-C power, then run:
   ```bash
   esptool --chip esp8266 --port <port> --after no-reset chip-id
   ```
   If `Chip is ESP8266EX` appears, the connection is correct.

- esptool 4.x uses `_` instead of `-` in commands (`erase_flash`, `write_flash`, `read_flash`, `--after no_reset`, `--flash_mode`, `--flash_size`).
- If `Failed to connect` appears, (1) swap TX and RX, and (2) power-cycle with GPIO0 still on GND, then retry.

## 6. Full erase + flash + verify

> **WARNING** — `erase-flash` irreversibly erases the entire 4MB (including the stock firmware, saved Wi-Fi, and the manufacturer activation state). The board decided not to back up the stock firmware (2026-11-13).

> **CAUTION** — Do not install this firmware through the stock firmware's web OTA (`/update_ota`). That does not erase the stock area. The first time, always erase and flash over serial.

### 6.1 Automatic (recommended)

```bash
cd hardware
tools/clean_flash.sh <port> smartclock-0.3.0-firmware.bin
```

- Runs chip check → full erase → write at `0x0` → read back 4MB → verify, in that order. At each step it asks you to "unplug and replug power with GPIO0 on GND, then press Enter".
- If your adapter has an auto-reset circuit (DTR/RTS), prefix the command with `AUTO_RESET=1`.
- The default speed is 115200bps (reading 4MB takes about 6 minutes). If your wiring is stable, add `BAUD=460800`.

### 6.2 Manual

```bash
# Tie GPIO0 to GND and power on. Unplug and replug power between each command (keep GPIO0 on GND).
esptool --chip esp8266 --port <port> --after no-reset chip-id
esptool --chip esp8266 --port <port> --after no-reset erase-flash
esptool --chip esp8266 --port <port> --after no-reset write-flash --flash-mode keep --flash-size keep 0x0 smartclock-0.3.0-firmware.bin
esptool --chip esp8266 --port <port> --after no-reset read-flash 0x0 0x400000 dump.bin
python3 tools/verify_flash.py dump.bin smartclock-0.3.0-firmware.bin
```

- Do not flash a LittleFS image. The firmware creates it on first boot.

### 6.3 Verification criteria (`tools/verify_flash.py`)

1. Checks that the dump is exactly 4MB.
2. Checks that the region from `0x0` for the length of the firmware matches the file. Differences in the 2nd and 3rd header bytes (flash mode and size) are reported only as `NOTE`.
3. Checks that the rest of the flash is entirely `0xFF`, in 4KB sectors.
4. Fails if any stock firmware string (`SD Pro`, `SDPro`, `Smart weather clock`, `update_ota`, `weatherkey`, `indexwifi.html`, `photo-setting.html`, `Device Key`, `Filename must start with`) is present.
5. The result is `RESULT: PASS` / `RESULT: FAIL`, with exit code 0 / 1.

- If you dumped after the device had already booted, add `--post-boot`. This allows only the regions the firmware writes to (LittleFS, EEPROM `0x3FB000`, RF calibration `0x3FC000`, Wi-Fi settings `0x3FD000`–`0x400000`).

## 7. First boot and Wi-Fi setup

1. Remove the GPIO0 jumper and power on again.
2. If no Wi-Fi is saved, the **setup mode** screen (`Setup mode` / `설정 모드`) appears. A new clock shows it in Korean.
   - AP name `SmartClock-XXXX` (last 4 digits of the chip ID) and AP password (random 8 characters per device)
   - Address `http://192.168.4.1`
   - Admin account `admin / <random 10 characters>` — **write this down.** It is the password for the settings page and for OTA.
3. Connect to that AP from a phone or PC and the setup page opens (if it does not, go to `http://192.168.4.1`).
4. Choose the language at the top (`English` / `한국어`), choose a network and enter the Wi-Fi password. To change the admin password, enter one with at least 8 characters (leave it empty to keep the value shown on screen). If the language changes, the clock's setup screen switches when you save.
5. Save. The clock restarts and connects to that network. The screen shows the IP address and host name (`smartclock-xxxx`) for 3 seconds.

- If the clock cannot join the saved Wi-Fi within 30 seconds, it opens setup mode for 5 minutes, then restarts and tries again.
- No HTTP response ever returns the Wi-Fi password, usage token, or admin password.
- The ESP8266 supports **2.4GHz Wi-Fi only**.

## 8. Settings web page

- Address: `http://<clock IP>/` or `http://smartclock-xxxx.local/`
- Authentication: HTTP Digest, user `admin`. After 10 consecutive failures, login is locked for 60 seconds.
- Every change requires a POST with a CSRF token that changes on every boot.

| Item | Description |
|---|---|
| Language / 언어 | `English` / `한국어`. Language of the settings page and of the clock screen. Section 8.3 |
| JSON URL | Usage JSON address. When used outside the office, `https://<domain>/clock/usage` |
| Bearer token | Relay server token. Leave empty to keep the existing value; check "clear token" to delete it. It is never displayed again |
| HTTPS certificate check | `CA` (default) / `Pinned SHA-1 fingerprint` / `No check (insecure)`. Section 8.1 |
| TLS SHA-1 fingerprint | Only for the fingerprint mode. 40 hex digits (`:` allowed) |
| Refresh interval | Fetch interval in minutes, default 10. On failure, retries every 2 minutes |
| POSIX time zone | Default `KST-9` |
| Brightness | 0–100 (0 = backlight off) |
| Screen design / Design change / Cycle interval | Screen design and auto-cycle (0.2.0). Section 8.2 |
| Admin password | At least 8 characters. Changing it also changes the OTA password |
| Reboot / Forget Wi-Fi / Factory reset | Restart / delete Wi-Fi only / delete Wi-Fi + all settings |

- Each item on the page has a short explanation under its name, in the chosen language.
- `GET /api/status` (authentication required): version, IP, RSSI, free heap, time, usage fetch status, `relay_status`, `tls_mode`, `waiting_for_time`, `lang`, `tls` memory figures.

> **CAUTION** — The device web page uses HTTP. The password itself is protected by Digest, but the settings contents are visible on the same network. Use it only on a trusted internal network.

### 8.1 HTTPS certificate check modes

| Mode | Behavior | When to use |
|---|---|---|
| `CA` (default) | Checks that the server certificate chain leads to the built-in root **ISRG Root X1 or X2** and is within its validity period | When the front-end nginx uses a **Let's Encrypt** certificate. No settings change is needed when the certificate is renewed |
| `fingerprint` | Compares only the server certificate's SHA-1 fingerprint | Certificates not from Let's Encrypt (internal CA, self-signed, other CAs). **When the certificate is renewed the fingerprint changes and the connection breaks** |
| `insecure` | No check | Testing only |

- **CA mode requires the correct time.** Until NTP time is received, fetching is deferred and `Waiting for time` (`시각 동기화 대기`) is shown.
- nginx must set `ssl_certificate` to **`fullchain.pem`**. If only `cert.pem` is given, the intermediate certificate is missing and the CA check fails (`server/README.md` Section 5.4).
- When a token is set, `https://` + `insecure` shows the warning `No cert check` (`인증서 검증 안 함`), and `http://` shows `Plaintext token` (`토큰 평문 전송`), in orange on the screen.
- Memory: nginx does not support TLS Max Fragment Length, so the connection uses a 16KB receive buffer and a 512B send buffer. Check the headroom with the `tls` value in `/api/status`.

| Field | Meaning |
|---|---|
| `heap_before` | Free heap just before connecting |
| `max_block_before` | Largest contiguous free block just before connecting. If it is smaller than 16,709 B, the connection fails |
| `heap_connected` | Free heap right after receiving the response headers (the lowest point) |
| `heap_after` | Free heap after the connection closes |
| `mfln` | Whether the server supports Max Fragment Length (`false` for nginx) |
| `ssl_error` | Last BearSSL error code. 0 = none |

> **NOTE** — If `heap_connected` is below 4,000 B or `max_block_before` is below 17,000 B, follow-up work to reduce memory use is needed. Record the values in the task.

### 8.2 Screen design

These are the three items in the `Display` group on the settings page. Saving changes the screen immediately, without a restart.

| Design B | Design C (default) |
|---|---|
| ![Clock running design B](docs/photo-design-b.jpg) | ![Clock running design C](docs/photo-design-c.jpg) |

Real device running 0.3.0 with Korean display.

| Item | Setting (`settings.json` key) | Default | Meaning |
|---|---|---|---|
| Screen design | `layout` = `A` / `B` / `C` | `C` | A concentric rings (outer Claude, inner Fable) / B clock on top + two 270° gauges below / C left and right semicircular rings (left Claude, right Fable) |
| Design change | `layout_mode` = `fixed` / `cycle` | `fixed` | `fixed` = keep the design above. `cycle` = cycle automatically in the order A→B→C, starting from the design above |
| Cycle interval | `cycle_sec` = 5–300 | 15 | Auto-cycle interval (seconds). Values out of range are rejected on save |

- When upgrading from 0.1.x, the three keys do not exist, so the clock starts with the defaults (`C`, `fixed`, 15 seconds). If the file contains invalid values, it also starts with the defaults.
- Saving settings during auto-cycle restarts the cycle from the design above.
- `layout`, `layout_mode`, and `cycle_sec` are added to `/api/status`.
- Gauge 1 = `items[0]` (Claude), gauge 2 = `items[1]` (Fable). A third item is not shown. Colors are the same as the 0.1.x bars (green below 70, yellow 70–89, red 90 and above). Each gauge shows its name (`label`) and a % number. If `pct` is `null` or there is no data, only the gray track is drawn and `--` is shown.
- The bottom line of the screen (for B, below the gauges) shows the first matching entry in the table below. If the width is insufficient, words are dropped from the end (e.g. in A, `Server error usage_http_5` → `Server error`).
- The table shows the English screen text, with the Korean screen text in parentheses.

| Priority | Situation | Display | Color |
|---|---|---|---|
| 1 | Wi-Fi disconnected (time is correct) | `Wi-Fi offline` (`Wi-Fi 연결 끊김`) | Orange |
| 2 | Section 10 status text is a warning | `Failed` (`실패`), `Failed · Nh ago` (`실패 · N시간 전`), `Stale · as of HH:MM` (`지연 · HH:MM 기준`), `Server error …` (`서버 오류 …`) | Orange |
| 3 | Token exposure warning | `No cert check` (`인증서 검증 안 함`) / `Plaintext token` (`토큰 평문 전송`) | Orange |
| 4 | `resets` present | A and C: `Reset 11-18 09:00` (`초기화 11-18 09:00`), B: `Reset 11-18 09:00 · as of 14:25` (`초기화 11-18 09:00 · 14:25 기준`) | Gray |
| 5 | Otherwise | Section 10 status text (`as of 14:25`, `Loading`, `No URL set`, etc.) | Gray |

- If the time is not yet set, `--:--` is shown in place of the time and `Syncing time` (`시간 동기화 중`) in place of the date (`Connecting Wi-Fi` (`Wi-Fi 연결 중`) before Wi-Fi connects), in orange.
- The screen redraws only the regions that changed. The time redraws only its box when the minute changes; a gauge redraws only its ring and number when its value changes. The whole screen is cleared only when the design or language changes and when returning from a message screen.

### 8.3 Display language (0.3.0)

One setting switches both the settings web page and the clock screen. Saving changes both immediately, without a restart.

| Item | Setting (`settings.json` key) | Default | Meaning |
|---|---|---|---|
| Language / 언어 | `lang` = `en` / `ko` | `ko` | `en` = English, `ko` = Korean |

- Where to change it: the `Language / 언어` group at the top of the settings page, or the same choice at the top of the first-boot setup portal (Section 7). The choices are always written `English` / `한국어`, whatever the current language.
- The setup portal page and the clock's setup mode screen follow the saved language.
- A clock upgraded from 0.2.0 has no `lang` key and starts in Korean, exactly as before. An invalid value also gives Korean. A factory reset returns to Korean.
- `lang` is added to `/api/status`.
- The gauge names (`Claude`, `Fable`) come from the relay server and are not translated.
- English clock screen text:

| Place | English | Korean |
|---|---|---|
| Date | `Sun, Nov 15` | `11월 15일 일요일` |
| Date, before time sync | `Syncing time` / `Connecting Wi-Fi` | `시간 동기화 중` / `Wi-Fi 연결 중` |
| Bottom line | Section 8.2 table | Section 8.2 table |
| Data age | `5m ago`, `3h ago`, `2d ago` | `5분 전`, `3시간 전`, `2일 전` |
| Setup mode screen | `Setup mode`, `Password`, `Open  http://192.168.4.1`, `Admin  admin / …` | `설정 모드`, `비밀번호`, `접속 주소  http://192.168.4.1`, `관리자  admin / …` |
| Message screens | `Connecting Wi-Fi`, `Updating`, `Done` / `Restarting`, `Error` | `Wi-Fi 연결 중`, `업데이트 중`, `완료` / `재시작`, `오류` |

- Memory: the web pages are sent in pieces of about 1 KB instead of being built whole in memory. In 0.2.0 the settings page held about 9 KB of heap while it was being sent (estimate); in 0.3.0 about 1.2 KB. The firmware's fixed RAM use is 816 B lower than 0.2.0, so the free heap shown by `/api/status` should be about 0.8 KB higher (`UNVERIFIED` on the real device).

## 9. OTA (over-the-air update)

Both methods require the admin password.

### 9.1 Web upload

1. Open `http://<clock IP>/update` and log in.
2. Choose the new `firmware.bin` and upload it. After "Update complete" appears, the clock restarts.
- Writing starts only if both Digest authentication and the CSRF token are valid. Anything that is not an ESP8266 image (`0xE9`) is rejected.

### 9.2 ArduinoOTA (PlatformIO)

```bash
export SMARTCLOCK_OTA_PASS='<admin password>'
pio run -e smartclock-ota -t upload --upload-port smartclock-xxxx.local   # or the IP
```
- Port 8266/UDP, MD5 challenge authentication.

> **CAUTION** — If firmware uploaded via OTA fails to boot, recovery is possible only over serial (Section 6). Test builds with changed pins over serial first.

### 9.3 0.2.0 → 0.3.0

All settings are kept (Wi-Fi, admin password, URL, token, check mode, time zone, brightness, screen design). The language starts as Korean, so the screen looks the same as before.

1. Download `smartclock-0.3.0-firmware.bin` and `SHA256SUMS` from Releases `hardware-v0.3.0` and check them with the Section 2 command. It must print `smartclock-0.3.0-firmware.bin: OK`.
2. On the same Wi-Fi as the clock, log in to `http://<clock IP>/update` and upload `smartclock-0.3.0-firmware.bin`. After "Update complete" appears, the clock restarts.
3. Check that `Firmware` (`펌웨어`) at the top of the settings page is `0.3.0`.
4. To use English, choose `English` in the `Language / 언어` group and save. The page and the clock screen switch at once.

#### Rolling back (0.3.0 → 0.2.0)

1. Upload `smartclock-0.2.0-firmware.bin` from Releases `hardware-v0.2.0` on the same `/update` page.
2. All settings are kept. The page returns to English only and the screen to Korean only.

- 0.2.0 does not read `lang`. **If you save settings in 0.2.0, the key is removed from the file.** If you then upgrade to 0.3.0 again, the language returns to Korean.

### 9.4 0.1.3 / 0.1.1 (LCD fix build) → 0.3.0

Wi-Fi, admin password, URL, token, check mode, time zone, and brightness settings are kept. Screen design settings start at the defaults (`C`, `fixed`, 15 seconds) and the language at Korean.

1. Download `smartclock-0.3.0-firmware.bin` and `SHA256SUMS` from Releases `hardware-v0.3.0` and check them with the Section 2 command. It must print `smartclock-0.3.0-firmware.bin: OK`.
2. On the same Wi-Fi as the clock, log in to `http://<clock IP>/update` and upload `smartclock-0.3.0-firmware.bin`. After "Update complete" appears, the clock restarts.
3. After the restart, check that the screen shows design C (left and right semicircular rings).
4. Check that `Firmware` (`펌웨어`) at the top of the settings page is `0.3.0`.
5. If needed, choose the language (Section 8.3) and the design and auto-cycle in the `Display` (`화면`) group, then save (Section 8.2).

When upgrading directly from 0.1.1 (LCD fix build), the check mode is set automatically from the existing settings. When upgrading from 0.1.3, the 0.1.3 check mode is kept as is.

| 0.1.1 setting | 0.3.0 check mode |
|---|---|
| Fingerprint entered | `fingerprint` |
| Fingerprint empty (0.1.1 did no check in this case) | `CA` |

> **CAUTION** — If in 0.1.1 the fingerprint was empty and the clock was connected to an HTTPS server that does not use Let's Encrypt, 0.3.0 switches to `CA` mode and fetching fails (`connection failed (TLS …)`). Enter a fingerprint or switch the nginx certificate to Let's Encrypt. `http://` addresses are not affected.

- To connect the usage display for the first time, enter the URL, token, and `CA` following `server/README.md` Section 6.

#### Rolling back (0.3.0 → 0.1.3)

1. Upload `smartclock-0.1.3-firmware.bin` from Releases `hardware-v0.1.3` on the same `/update` page.
2. Wi-Fi, password, URL, token, and check mode are kept. The screen returns to the 0.1.3 bar display.

- 0.1.3 does not read the design and language keys (`layout`, `layout_mode`, `cycle_sec`, `lang`). **If you save settings in 0.1.3, these keys are removed from the file.** If you then upgrade to 0.3.0 again, the design returns to the defaults (`C`, `fixed`, 15 seconds) and the language to Korean. If you did not save in 0.1.3, the previous design settings come back intact.
- Do not roll back to 0.1.2 (Section 0 CAUTION).

## 10. Usage JSON contract

The authoritative contract is Section 2 of [`../server/README.md`](../server/README.md). If you change the contract, update both documents together. The firmware handles it as follows.

The request header is `Accept: application/json`, plus `Authorization: Bearer <token>` if a token is set. The body must be 4,096 bytes or less, and **redirects are not followed.** Anything other than HTTP 200 is a fetch failure.

| Field | Firmware handling |
|---|---|
| `items` | Required. 0.2.0 and later show the first 2 as gauges (`items[0]` Claude, `items[1]` Fable). Items from the 3rd onward are not shown |
| `items[].label` | Shown as the gauge name. Truncated beyond 16 bytes. ASCII recommended |
| `items[].value` | Not shown on the 0.2.0 and later screens. The gauge number uses `pct` |
| `items[].pct` | If a number: gauge and `N%` (green below 70, yellow 70–89, red 90 and above). If `null`: gray track and `--` |
| `items[].resets` | If `MM-DD HH:MM`, the bottom line shows `Reset 11-18 09:00` (`초기화 11-18 09:00`) (Section 8.2 priority table) |
| `updated` | Status text `as of HH:MM` (`HH:MM 기준`). If `null`, `Nm ago` (`N분 전`) |
| `status` / `error` | See the status text table below |

Status text (the first matching row from the top). 0.1.x shows it on the right of the header; 0.2.0 and later show it on the bottom line according to the Section 8.2 priorities. The English screen text is shown with the Korean screen text in parentheses.

| Situation | Display | Color |
|---|---|---|
| URL not set | `No URL set` (`URL 미설정`) | Gray |
| CA mode but time not synchronized | `Waiting for time` (`시각 동기화 대기`) | Gray |
| Nothing received yet | `Loading` (`불러오는 중`) / on failure `Failed` (`실패`) | Gray / Orange |
| Clock → relay server fetch failed | `Failed · Nm ago` (`실패 · N분 전`) | Orange |
| `status:"stale"` | `Stale · as of HH:MM` (`지연 · HH:MM 기준`) | Orange |
| `status:"error"` | `Server error <first 12 chars of error>` (`서버 오류 …`) | Orange |
| Normal | `as of HH:MM` (`HH:MM 기준`) | Gray |

- On the 0.1.x bar screen, when orange text appears, the title (`최근 7일 사용량`, "usage, last 7 days") is hidden and only the text is shown.

## 11. Troubleshooting

| Symptom | Cause / action |
|---|---|
| Repeated `Exception (9)` right after boot, no picture | This is 0.1.0 or 0.1.2. Flash 0.3.0 (or 0.1.3) over serial |
| `Failed to connect` (esptool) | See the end of Section 5. Swap TX/RX, GPIO0–GND, power-cycle |
| Settings page shows `failed: HTTP 401` (`실패: HTTP 401`) | Token mismatch |
| `failed: HTTP 429` | The relay server blocked for 15 minutes after repeated wrong tokens, or the nginx request rate limit. `server/README.md` Section 7 |
| `failed: HTTP 301` / `302` | The URL is `http://` but nginx redirects to HTTPS. Change the URL to `https://` |
| `failed: HTTP 404` | Typo in the path. It must be exactly `/clock/usage` |
| `failed: connection failed (TLS …)` | Certificate check failure or out of memory. Check the chain per `server/README.md` Section 5.6, and the `tls` values per Section 8.1 |
| Bottom line keeps showing `Waiting for time` (`시각 동기화 대기`) | NTP (UDP 123) is blocked on the network. Check the router settings |
| Screen colors look wrong | Invert `LCD_BGR` / `LCD_INVERT` and rebuild |
| Screen design changes by itself | `Design change` is set to `cycle`. Change it to `fixed` and save (Section 8.2) |
| `Cycle interval must be 5-300 seconds.` (`순환 간격은 5~300초여야 합니다.`) when saving settings | The cycle interval accepts only 5–300 seconds |
| Fable gauge stays gray with `--` | The relay server sends `items[1].pct` as `null`. This happens when the account has no Fable weekly limit row (`server/README.md` Section 1.4) |
| After rolling back to 0.1.3 and reinstalling 0.3.0, the design reverted to C | Saving settings in 0.1.3 erases the design settings (Section 9.4, Rolling back). Choose the design again and save |
| Screen or page is in the wrong language | Choose the language in the `Language / 언어` group at the top of the settings page and save (Section 8.3). After a rollback to 0.2.0 and a save there, 0.3.0 starts in Korean again |
| After uploading, `Firmware` is not 0.3.0 | The upload was interrupted before it finished. Upload the same file again. If the clock does not boot, use Section 6 (serial) |

## 12. File layout

| Path | Contents |
|---|---|
| `platformio.ini` | 3 envs: `smartclock`, `smartclock-ota`, `lcd-diag` |
| `src/main.cpp` | Boot sequence, Wi-Fi, NTP, ArduinoOTA, main loop |
| `src/lcd.cpp` | ST7789 driver, font renderer, anti-aliased arcs (`drawRing`) |
| `src/ui.cpp` | Layout of screen designs A, B, C and partial updates |
| `include/i18n.h` / `src/i18n.cpp` | Clock screen text in English and Korean |
| `include/layout_cycle.h` | Fixed/auto-cycle design selection |
| `src/web.cpp` | Setup portal, admin page, `/update` request handling |
| `include/web_pages.h` / `src/web_pages.cpp` | Web page text in English and Korean, HTML pages sent in chunks |
| `src/usage.cpp` / `src/usage_model.cpp` | Usage fetch (HTTP/HTTPS) / JSON parsing and header text |
| `src/settings.cpp` / `src/settings_model.cpp` | Settings storage / settings conversion, check mode, and warning decisions |
| `src/ca_roots_gen.cpp`, `include/ca_roots.h` | Built-in root certificates |
| `src/diag.cpp` | Screen pin diagnostics |
| `src/fonts_gen.cpp` | Generated font tables |
| `tools/clean_flash.sh`, `tools/verify_flash.py` | Erase, flash, verify |
| `tools/gen_fonts.py`, `tools/gen_ca_roots.py`, `tools/glyphs_ko.txt` | Font and root certificate generation |
| `test/pc/` | PC tests (`run.sh`, `test_usage.cpp`, Arduino API stand-in headers) |
| `test/pc/sim/` | Screen PC renderer (fake ST7789), partial update / overlap / cycle / language switch checks, PNG comparison sheet script |
| `test/pc/web/` | Web page PC renderer (HTML files in both languages) |
| `docs/wiring.png` | Wiring location photo for the first serial flash (Section 4.2) |
| `docs/photo-design-b.jpg`, `docs/photo-design-c.jpg` | Real device photos of designs B and C (Section 8.2) |
