[English](README.md) | **한국어**

# SmartClock 펌웨어 (hardware) — 0.2.0

SD Pro 스마트 클락(ESP-12F / ESP8266, 4MB flash, ST7789 240×240 LCD)에 올리는 자체 펌웨어입니다. 처음부터 새로 작성했고 순정 펌웨어의 코드·폰트·이미지는 쓰지 않습니다.

- 화면: 시각(`HH:MM`)·날짜(`11월 15일 일요일`)와 Claude·Fable 주간 사용률 원형 게이지 2개 (NTP, 기본 시간대 `KST-9`)
- 화면 디자인 3종(A 동심원 링 / B 위 시계 + 아래 게이지 / C 좌우 반원 링, 기본 C). 하나로 고정하거나 자동 순환합니다(8.2절)
- 사용량: 중계 서버 `server/` 가 주는 JSON 을 받아 그림
- 무선 업데이트(OTA): 웹 업로드 `/update`(Digest 인증 + CSRF 토큰) + ArduinoOTA(비밀번호 필수)
- 설정 웹페이지: 사용량 URL·토큰, HTTPS 인증서 검증 방식, 시간대, 밝기, 관리자 비밀번호, Wi-Fi 재설정, 공장 초기화
- HTTPS 인증서 검증: 기본은 내장 루트 인증서(ISRG Root X1·X2 = Let's Encrypt) 검증

중계 서버 설치·외부 공개(nginx)는 [`../server/README.ko.md`](../server/README.ko.md) 에 있습니다.

## 0. 버전

> **CAUTION** — 0.1.1 은 서로 다른 두 빌드가 같은 버전 문자열을 씁니다. 0.1.2 에는 LCD 정렬 수정이 빠져 있어 **실기기에서 부팅 직후 `Exception (9)` 로 재시작을 반복합니다**(0.1.0 실기기 로그로 확인, 0.1.2 는 같은 코드). 0.1.2 는 쓰지 말고 0.2.0(또는 0.1.3)을 쓰십시오. 0.1.3 · 0.1.1(LCD 수정판)에서 0.2.0 으로 올리는 절차는 9.3절에 있습니다.

> **NOTE** — 0.2.0 은 실기기 확인 전입니다. 1장 표의 `UNVERIFIED`(미확인) 항목은 시계에 올린 뒤 확인합니다.

## 1. 검증 상태

| 항목 | 상태 |
|---|---|
| `pio run -e smartclock` 빌드 (0.2.0) | `CONFIRMED` — RAM 42.1% (34,496 B), Flash 59.2% (618,823 B), `firmware.bin` 622,976 B |
| `pio run -e lcd-diag` 빌드 (0.2.0) | `CONFIRMED` — RAM 35.0% (28,640 B), Flash 31.4% (327,835 B), `firmware.bin` 331,984 B |
| PC 시험 (`test/pc/run.sh`) | `CONFIRMED` — 11개 묶음 148개 검사 통과. JSON 해석, 상태 문구, 화면 배치 폭, 설정 저장·이전(디자인 설정 키 포함), 검증 방식·토큰 경고 판정 |
| 화면 3종 렌더링·부분 갱신 (`test/pc/sim/run.sh`) | `CONFIRMED`(PC) — 실제 `lcd.cpp`·`ui.cpp` 가 보내는 SPI 바이트를 가짜 ST7789 로 받아 그림. 3종 × 7상태 = 21장에서 글자 상자와 링 픽셀 겹침 0. 같은 분 안 590프레임 전송 0 px, 분 변경 시 시각 상자 6,450 px 만, 디자인 전환 시 1회 전체 |
| 자동 순환 타이밍 (`include/layout_cycle.h`) | `CONFIRMED`(PC) — 순서 A→B→C, 간격, 저장 시 처음부터, `millis()` 32비트 넘김 |
| 설정 이전 (0.1.x → 0.2.0) | `CONFIRMED`(PC) — 0.1.x 형식 `settings.json` 을 0.2.0 이 읽으면 기존 값 유지 + 디자인 기본값(`C`·`fixed`·15초). 0.1.x 가 0.2.0 형식 파일을 읽어도 기존 항목은 그대로 읽힘 |
| LCD 핀 기본값 (3장) | `CONFIRMED` — 2026-11-14 보드 실기기(LCD 수정판 0.1.1)에서 화면 표시 |
| 시리얼 기록 절차 (CP2102, macOS) | `CONFIRMED` — 2026-11-14 보드가 같은 절차로 기록 |
| 내장 루트 인증서 | `CONFIRMED` — `tools/gen_ca_roots.py` 가 SHA-256 지문을 고정값과 대조. ISRG Root X1 `96bcec06…bddf08c6`, X2 `69729b8e…3ccb1470` |
| 0.2.0 실기기 화면(색상·배치·원호 그리기 시간) | `UNVERIFIED` — 실기기 필요. 디자인 전환 1회에 링 픽셀 약 2만~3만 개를 부동소수점으로 계산합니다 |
| Wi-Fi 설정 포털, NTP, 사용량 조회, OTA 2종 | `UNVERIFIED` — 실기기 확인 결과가 아직 없습니다 |
| CA 검증으로 실제 Let's Encrypt 서버(nginx) 접속 | `UNVERIFIED` — 실제 도메인 필요 |
| HTTPS 접속 중 힙 여유 | `UNVERIFIED` — `/api/status` 의 `tls` 값으로 확인합니다(8.1절) |

## 2. 빠른 시작

| 하려는 일 | 읽을 곳 |
|---|---|
| 순정 시계에 처음 기록 | 4장 → 5장 → 6장 → 7장 |
| 이미 우리 펌웨어가 올라간 시계를 0.2.0 으로 | 9.3절 |
| 화면 디자인 바꾸기·자동 순환 | 8.2절 |
| 사용량 표시 연결 | `server/README.ko.md` 6장(시계 설정) |
| 화면이 안 나옴 | 3.1절 |
| 소스에서 직접 빌드 | 2.1절 |

빌드된 파일은 GitHub Releases 의 `hardware-v0.2.0` 에 있습니다. 이전 판은 `hardware-v0.1.3` 에 남아 있습니다.

| 파일 | 용도 |
|---|---|
| `smartclock-0.2.0-firmware.bin` | 본 펌웨어 |
| `lcd-diag-0.2.0-firmware.bin` | 화면 핀 진단 (Wi-Fi 없음) |
| `SHA256SUMS` | 파일 확인용 |

```bash
sha256sum -c --ignore-missing SHA256SUMS      # macOS: shasum -a 256 -c --ignore-missing SHA256SUMS
```

### 2.1 소스에서 빌드

```bash
cd hardware
python3 -m pip install platformio "esptool>=5"
pio run -e smartclock             # → .pio/build/smartclock/firmware.bin
pio run -e lcd-diag               # → .pio/build/lcd-diag/firmware.bin
test/pc/run.sh                    # PC 시험. g++ 필요. smartclock 을 한 번 빌드한 뒤 실행
test/pc/sim/run.sh out/           # 화면 3종 PC 렌더 + 부분 갱신 검사 → out/*.ppm
python3 test/pc/sim/make_sheet.py out/ png/   # ppm → png 와 비교표 (Pillow 필요)
python3 tools/gen_fonts.py        # 화면 글자를 바꿀 때만 (결과 src/fonts_gen.cpp 는 이미 들어 있음)
python3 tools/gen_ca_roots.py     # 루트 인증서를 바꿀 때만 (결과 src/ca_roots_gen.cpp 는 이미 들어 있음)
```

- 플랫폼 `espressif8266@4.2.1` (Arduino 코어 3.1.2), 보드 `esp12e`, flash 4MB **DOUT 모드**, CPU 160MHz.
- flash 분할 `eagle.flash.4m1m.ld`: 펌웨어 약 1MB, OTA 임시 영역 약 2MB, LittleFS 1MB(`0x300000`–`0x3FA000`).
- 외부 라이브러리는 ArduinoJson 7.4.2 하나입니다.
- 화면 글꼴은 Noto Sans CJK KR(SIL OFL 1.1)에서 필요한 글자만 뽑았습니다. ASCII + `tools/glyphs_ko.txt` 의 한글만 있고, 없는 글자는 `?` 로 나옵니다.
- 첫 빌드는 툴체인을 받느라 수 분이 걸립니다.

## 3. LCD 핀 설정

모든 핀·패널 옵션은 `platformio.ini` 의 `[lcd] build_flags` **한 줄**에 있습니다. 값을 바꾸고 다시 빌드합니다.

| 플래그 | 기본값 | 뜻 |
|---|---|---|
| `LCD_MOSI` / `LCD_SCLK` | 13 / 14 | SPI 데이터·클럭. 13/14 이면 하드웨어 SPI, 그 외 값이면 소프트웨어 SPI(느림) |
| `LCD_CS` | -1 | 칩 선택. -1 = 사용 안 함 |
| `LCD_DC` | 0 | 데이터/명령 선택 |
| `LCD_RST` | 2 | 리셋. -1 = 사용 안 함 |
| `LCD_BL` | 5 | 백라이트 |
| `LCD_BL_ACTIVE_LOW` | 1 | 1 = LOW 일 때 켜짐 |
| `LCD_SPI_MODE` | 3 | CS 없는 ST7789 는 보통 3 |
| `LCD_INVERT` | 1 | IPS 패널 색 반전 |
| `LCD_BGR` | 0 | 빨강·파랑이 바뀌어 보이면 1 |
| `LCD_ROTATION` | 0 | 0–3 (90° 단위) |

기본값은 2026-11-14 보드 실기기에서 화면이 나온 값입니다. 다른 계열 기기에 쓸 때만 3.1절로 핀을 찾습니다.

### 3.1 화면이 안 나올 때 — 핀 진단 빌드 (`lcd-diag`)

1. `lcd-diag-0.2.0-firmware.bin` 을 6장 절차로 기록합니다.
2. 진단 펌웨어는 핀 후보 16조합 × SPI 모드 2 × 백라이트 극성 2 = **64단계**를 4초씩 돌립니다(한 바퀴 약 4분 16초).
3. 화면이 보이는 단계의 큰 숫자와 아래 두 줄(`DC0 RST2 CS-1 BL5`, `MODE3 BL-LOW RED`)을 적습니다. 시리얼(115200bps)에도 같은 내용이 찍힙니다.
4. 배경색이 글자(`RED`/`GREEN`/`BLUE`)와 다르면 `LCD_BGR` 을 반대로 바꿉니다.
5. 적은 값을 `[lcd] build_flags` 에 넣고 `smartclock` 을 다시 빌드합니다.
6. 64단계 모두 안 보이면 화면 케이블(FPC)이 꽂히는 기판 쪽 커넥터와 주변 배선 사진(앞·뒤)을 찍어 태스크에 남겨 주십시오.

시리얼 로그 보기:
```bash
python3 -m serial.tools.miniterm <포트> 115200 --dtr 0 --rts 0
```
부팅 직후 깨진 글자 한 줄은 정상입니다(ESP8266 부트롬이 74880bps 로 찍는 문구).

## 4. 준비물과 배선

### 4.1 USB-시리얼 어댑터

| 제품 | 판정 |
|---|---|
| **CP2102** | 권장. 신호선이 항상 3.3V 라 점퍼 위치와 관계없이 안전합니다(VCC 를 연결하지 않을 때) |
| CH340G | 사용 가능. 점퍼 하나로 신호 전압이 함께 바뀌므로 **3.3V 에 둡니다** |
| USBasp(HW-437) | **사용 불가.** AVR 전용 ISP 기록기이고 시리얼 포트가 생기지 않습니다 |

> **WARNING** — ESP8266 단자는 3.3V 용(최대 3.6V)입니다. 5V 신호가 RX 단자에 들어가면 칩이 손상될 수 있습니다. 연결 전에 어댑터만 PC 에 꽂고 멀티미터로 **TX ↔ GND** 를 재십시오. 3.0–3.4V 이면 연결합니다. 4.5V 이상이면 점퍼를 바꾸고 다시 잽니다.

### 4.2 배선

| 어댑터 | ESP-12F 패드 |
|---|---|
| TX | RXD (GPIO3) |
| RX | TXD (GPIO1) |
| GND | GND |
| VCC | **연결하지 않습니다.** 전원은 시계 USB-C 로 넣습니다 |
| — | **GPIO0 → GND** 점퍼: 다운로드 모드 진입용. 지우기~검증 내내 붙여 둡니다 |

- 어댑터와 시계의 GND 를 반드시 공통으로 묶습니다.
- GPIO0 을 GND 에 붙인 채 전원을 넣으면 다운로드 모드로 켜집니다. 이 상태에서는 어떤 펌웨어도 실행되지 않습니다.
- 배선 위치 사진은 SOO-596 첨부 `SOO-596-wiring.png` 에 있습니다.

## 5. 기록 프로그램 설치 (esptool)

1. Python 3 를 설치합니다(없을 때만). Windows 는 설치 화면에서 "Add python.exe to PATH" 를 체크합니다.
2. esptool 과 시리얼 모니터를 설치합니다.
   ```bash
   python3 -m pip install "esptool>=5" pyserial
   ```
3. Windows 에서 포트가 보이지 않으면 Silicon Labs "CP210x VCP Driver" 를 설치합니다. macOS·Linux 는 대부분 드라이버가 필요 없습니다.
4. 어댑터를 꽂고 포트 이름을 확인합니다.

| OS | 확인 방법 | 예 |
|---|---|---|
| Windows | 장치 관리자 → 포트(COM & LPT) | `COM3` |
| macOS | `ls /dev/cu.*` | `/dev/cu.usbserial-0001`, `/dev/cu.SLAB_USBtoUART` |
| Linux | `ls /dev/ttyUSB*` | `/dev/ttyUSB0` (권한 오류 시 `sudo usermod -aG dialout $USER` 후 재로그인) |

5. 연결을 확인합니다. GPIO0 을 GND 에 둔 채 시계 USB-C 전원을 뺐다 꽂고 실행합니다.
   ```bash
   esptool --chip esp8266 --port <포트> --after no-reset chip-id
   ```
   `Chip is ESP8266EX` 가 나오면 연결이 맞습니다.

- esptool 4.x 는 명령의 `-` 를 `_` 로 바꿉니다(`erase_flash`, `write_flash`, `read_flash`, `--after no_reset`, `--flash_mode`, `--flash_size`).
- `Failed to connect` 가 나오면 ① TX·RX 를 서로 바꿔 꽂고 ② GPIO0 이 GND 에 붙은 상태로 전원을 다시 넣은 뒤 재시도합니다.

## 6. 완전 삭제 + 기록 + 검증

> **WARNING** — `erase-flash` 는 4MB 전체(순정 펌웨어, 저장된 Wi-Fi, 제조사 활성화 상태 포함)를 되돌릴 수 없게 지웁니다. 순정 펌웨어 백업은 하지 않기로 보드가 결정했습니다(2026-11-13).

> **CAUTION** — 순정 펌웨어의 웹 OTA(`/update_ota`)로 이 펌웨어를 올리지 않습니다. 순정 영역이 지워지지 않습니다. 처음 한 번은 반드시 시리얼로 지우고 기록합니다.

### 6.1 자동 (권장)

```bash
cd hardware
tools/clean_flash.sh <포트> smartclock-0.2.0-firmware.bin
```

- 칩 확인 → 전체 삭제 → `0x0` 에 기록 → 4MB 읽기 → 검증을 차례로 합니다. 단계마다 "GPIO0 을 GND 에 둔 채 전원을 뺐다 꽂고 Enter" 를 요청합니다.
- 어댑터에 자동 리셋 회로(DTR/RTS)가 있으면 앞에 `AUTO_RESET=1` 을 붙입니다.
- 기본 속도는 115200bps 입니다(4MB 읽기 약 6분). 배선이 안정적이면 `BAUD=460800` 을 붙입니다.

### 6.2 수동

```bash
# GPIO0 을 GND 에 붙이고 전원 투입. 각 명령 사이에 전원을 뺐다 꽂습니다(GPIO0 은 계속 GND).
esptool --chip esp8266 --port <포트> --after no-reset chip-id
esptool --chip esp8266 --port <포트> --after no-reset erase-flash
esptool --chip esp8266 --port <포트> --after no-reset write-flash --flash-mode keep --flash-size keep 0x0 smartclock-0.2.0-firmware.bin
esptool --chip esp8266 --port <포트> --after no-reset read-flash 0x0 0x400000 dump.bin
python3 tools/verify_flash.py dump.bin smartclock-0.2.0-firmware.bin
```

- LittleFS 이미지는 기록하지 않습니다. 펌웨어가 첫 부팅 때 스스로 만듭니다.

### 6.3 검증 기준 (`tools/verify_flash.py`)

1. 덤프가 정확히 4MB 인지 확인합니다.
2. `0x0` 부터 펌웨어 길이만큼이 파일과 같은지 확인합니다. 헤더 2·3번째 바이트(flash 모드·크기) 차이는 `NOTE` 로만 보고합니다.
3. 나머지 영역이 4KB 섹터 단위로 전부 `0xFF` 인지 확인합니다.
4. 순정 펌웨어 문자열(`SD Pro`, `SDPro`, `Smart weather clock`, `update_ota`, `weatherkey`, `indexwifi.html`, `photo-setting.html`, `Device Key`, `Filename must start with`)이 하나라도 있으면 실패입니다.
5. 결과는 `RESULT: PASS` / `RESULT: FAIL`, 종료 코드 0 / 1 입니다.

- 이미 부팅한 뒤 덤프했다면 `--post-boot` 를 붙입니다. 펌웨어가 쓰는 영역(LittleFS, EEPROM `0x3FB000`, RF 보정 `0x3FC000`, Wi-Fi 설정 `0x3FD000`–`0x400000`)만 허용합니다.

## 7. 첫 부팅과 Wi-Fi 설정

1. GPIO0 점퍼를 떼고 전원을 다시 넣습니다.
2. 저장된 Wi-Fi 가 없으면 **설정 모드** 화면이 나옵니다.
   - AP 이름 `SmartClock-XXXX`(칩 ID 끝 4자리)와 AP 비밀번호(기기마다 무작위 8자)
   - 접속 주소 `http://192.168.4.1`
   - 관리자 계정 `admin / <무작위 10자>` — **이 값을 적어 둡니다.** 설정 페이지와 OTA 비밀번호입니다.
3. 휴대폰·PC 로 그 AP 에 접속하면 설정 페이지가 뜹니다(안 뜨면 `http://192.168.4.1`).
4. 네트워크를 고르고 Wi-Fi 비밀번호를 넣습니다. 관리자 비밀번호를 바꾸려면 8자 이상으로 입력합니다(비우면 화면의 값 유지).
5. 저장하면 재시작해 그 네트워크에 접속합니다. 화면에 IP 주소와 호스트 이름(`smartclock-xxxx`)이 3초간 나옵니다.

- 저장된 Wi-Fi 에 30초 안에 붙지 못하면 설정 모드를 5분간 열었다가 재시작해 다시 시도합니다.
- 어떤 HTTP 응답에도 Wi-Fi 비밀번호·사용량 토큰·관리자 비밀번호를 돌려주지 않습니다.
- ESP8266 은 **2.4GHz Wi-Fi 만** 지원합니다.

## 8. 설정 웹페이지

- 주소: `http://<시계 IP>/` 또는 `http://smartclock-xxxx.local/`
- 인증: HTTP Digest, 사용자 `admin`. 연속 10회 실패하면 60초간 잠깁니다.
- 모든 변경은 POST + 부팅마다 바뀌는 CSRF 토큰이 있어야 처리합니다.

| 항목 | 설명 |
|---|---|
| JSON URL | 사용량 JSON 주소. 회사 밖에서 쓰면 `https://<도메인>/clock/usage` |
| Bearer token | 중계 서버 토큰. 비우면 기존 값 유지, "clear token" 체크로 삭제. 다시 표시되지 않습니다 |
| HTTPS certificate check | `CA`(기본) / `Pinned SHA-1 fingerprint` / `No check (insecure)`. 8.1절 |
| TLS SHA-1 fingerprint | fingerprint 방식일 때만. 40자리 hex(`:` 허용) |
| Refresh interval | 조회 주기(분), 기본 10. 실패하면 2분 간격 재시도 |
| POSIX time zone | 기본 `KST-9` |
| Brightness | 0–100 (0 = 백라이트 끔) |
| Screen design / Design change / Cycle interval | 화면 디자인·자동 순환 (0.2.0). 8.2절 |
| Admin password | 8자 이상. 바꾸면 OTA 비밀번호도 함께 바뀝니다 |
| Reboot / Forget Wi-Fi / Factory reset | 재시작 / Wi-Fi 만 삭제 / Wi-Fi + 모든 설정 삭제 |

- `GET /api/status` (인증 필요): 버전, IP, RSSI, 남은 힙, 시각, 사용량 조회 상태, `relay_status`, `tls_mode`, `waiting_for_time`, `tls` 메모리 수치.

> **CAUTION** — 기기 웹페이지는 HTTP 입니다. 비밀번호 자체는 Digest 로 보호되지만 설정 내용은 같은 네트워크에서 볼 수 있습니다. 신뢰하는 내부망에서만 씁니다.

### 8.1 HTTPS 인증서 검증 방식

| 방식 | 동작 | 쓰는 경우 |
|---|---|---|
| `CA` (기본) | 서버 인증서 체인이 내장 루트 **ISRG Root X1 또는 X2** 로 이어지는지, 유효 기간 안인지 검사 | 앞단 nginx 가 **Let's Encrypt** 인증서를 쓸 때. 인증서가 갱신돼도 설정을 바꿀 필요가 없습니다 |
| `fingerprint` | 서버 인증서 SHA-1 지문만 비교 | Let's Encrypt 가 아닌 인증서(사내 CA, 자체 서명, 다른 인증기관). **인증서가 갱신되면 지문이 바뀌어 끊깁니다** |
| `insecure` | 검사 안 함 | 시험용만 |

- **CA 방식은 시각이 맞아야 합니다.** NTP 시각을 받기 전에는 조회를 미루고 `시각 동기화 대기` 를 표시합니다.
- nginx 는 `ssl_certificate` 에 **`fullchain.pem`** 을 지정해야 합니다. `cert.pem` 만 주면 중간 인증서가 빠져 CA 검증이 실패합니다(`server/README.ko.md` 5.4절).
- 토큰이 설정된 상태에서 `https://` + `insecure` 이면 `인증서 검증 안 함`, `http://` 이면 `토큰 평문 전송` 경고가 화면에 주황색으로 나옵니다.
- 메모리: nginx 는 TLS Max Fragment Length 를 지원하지 않으므로 수신 16KB·송신 512B 버퍼로 접속합니다. `/api/status` 의 `tls` 값으로 여유를 봅니다.

| 필드 | 뜻 |
|---|---|
| `heap_before` | 접속 직전 남은 힙 |
| `max_block_before` | 접속 직전 가장 큰 연속 빈 블록. 16,709 B 보다 작으면 접속이 실패합니다 |
| `heap_connected` | 응답 헤더 수신 직후 남은 힙 (가장 낮은 지점) |
| `heap_after` | 연결 종료 후 남은 힙 |
| `mfln` | 서버의 Max Fragment Length 지원 여부 (nginx 는 `false`) |
| `ssl_error` | 마지막 BearSSL 오류 코드. 0 = 없음 |

> **NOTE** — `heap_connected` 가 4,000 B 아래이거나 `max_block_before` 가 17,000 B 아래이면 메모리를 줄이는 후속 작업이 필요합니다. 값을 태스크에 남겨 주십시오.

### 8.2 화면 디자인 (0.2.0)

설정 페이지 `Display` 묶음의 세 항목입니다. 저장하면 재시작 없이 바로 화면이 바뀝니다.

| 항목 | 설정값 (`settings.json` 키) | 기본 | 뜻 |
|---|---|---|---|
| Screen design | `layout` = `A` / `B` / `C` | `C` | A 동심원 링(바깥 Claude, 안쪽 Fable) / B 위 시계 + 아래 270° 게이지 2개 / C 좌우 반원 링(왼쪽 Claude, 오른쪽 Fable) |
| Design change | `layout_mode` = `fixed` / `cycle` | `fixed` | `fixed` = 위 디자인 고정. `cycle` = 위 디자인부터 A→B→C 순서로 자동 순환 |
| Cycle interval | `cycle_sec` = 5–300 | 15 | 자동 순환 간격(초). 범위 밖이면 저장을 거부합니다 |

- 0.1.x 에서 올리면 세 키가 없으므로 기본값(`C`·`fixed`·15초)으로 켜집니다. 파일에 잘못된 값이 있어도 기본값으로 켜집니다.
- 자동 순환 중에 설정을 저장하면 순환을 위 디자인부터 다시 시작합니다.
- `/api/status` 에 `layout`·`layout_mode`·`cycle_sec` 가 추가됩니다.
- 게이지 1 = `items[0]`(Claude), 게이지 2 = `items[1]`(Fable). 3번째 항목은 표시하지 않습니다. 색은 0.1.x 막대와 같습니다(70 미만 초록, 70–89 노랑, 90 이상 빨강). 게이지마다 이름(`label`)과 % 숫자를 씁니다. `pct` 가 `null` 이거나 데이터가 없으면 회색 트랙만 그리고 `--` 를 씁니다.
- 화면 맨 아래 한 줄(B 는 게이지 아래)에 아래 표에서 먼저 맞는 것 하나를 표시합니다. 폭이 모자라면 뒤쪽 단어부터 뺍니다(예: A 에서 `서버 오류 usage_http_5` → `서버 오류`).

| 우선 | 상황 | 표시 | 색 |
|---|---|---|---|
| 1 | Wi-Fi 끊김(시각은 맞음) | `Wi-Fi 연결 끊김` | 주황 |
| 2 | 10장 상태 문구가 경고일 때 | `실패`, `실패 · N분 전`, `지연 · HH:MM 기준`, `서버 오류 …` | 주황 |
| 3 | 토큰 노출 경고 | `인증서 검증 안 함` / `토큰 평문 전송` | 주황 |
| 4 | `resets` 있음 | A·C `초기화 11-18 09:00`, B `초기화 11-18 09:00 · 14:25 기준` | 회색 |
| 5 | 그 밖 | 10장 상태 문구(`14:25 기준`, `불러오는 중`, `URL 미설정` 등) | 회색 |

- 시각이 아직 맞지 않으면 시각 자리에 `--:--`, 날짜 자리에 `시간 동기화 중`(Wi-Fi 연결 전이면 `Wi-Fi 연결 중`)을 주황으로 표시합니다.
- 화면은 바뀐 부분만 다시 그립니다. 시각은 분이 바뀔 때 시각 상자만, 게이지는 값이 바뀔 때 그 링과 숫자만 다시 그립니다. 전체를 지우는 것은 디자인이 바뀔 때와 메시지 화면에서 돌아올 때뿐입니다.

## 9. OTA (무선 업데이트)

두 방식 모두 관리자 비밀번호가 필요합니다.

### 9.1 웹 업로드

1. `http://<시계 IP>/update` 에 접속해 로그인합니다.
2. 새 `firmware.bin` 을 고르고 업로드합니다. "완료" 후 재시작합니다.
- Digest 인증과 CSRF 토큰이 모두 맞아야 쓰기를 시작합니다. ESP8266 이미지(`0xE9`)가 아니면 거부합니다.

### 9.2 ArduinoOTA (PlatformIO)

```bash
export SMARTCLOCK_OTA_PASS='<관리자 비밀번호>'
pio run -e smartclock-ota -t upload --upload-port smartclock-xxxx.local   # 또는 IP
```
- 포트 8266/UDP, MD5 챌린지 인증입니다.

> **CAUTION** — OTA 로 올린 펌웨어가 부팅에 실패하면 복구는 시리얼(6장)로만 됩니다. 핀을 바꾼 빌드는 먼저 시리얼로 시험합니다.

### 9.3 0.1.3 · 0.1.1(LCD 수정판) → 0.2.0

Wi-Fi·관리자 비밀번호·URL·토큰·검증 방식·시간대·밝기 설정은 그대로 남습니다. 화면 디자인 설정은 기본값(`C`·`fixed`·15초)으로 시작합니다.

1. Releases `hardware-v0.2.0` 에서 `smartclock-0.2.0-firmware.bin` 과 `SHA256SUMS` 를 받아 2장 명령으로 확인합니다. `smartclock-0.2.0-firmware.bin: OK` 가 나와야 합니다.
2. 시계와 같은 Wi-Fi 에서 `http://<시계 IP>/update` 에 로그인하고 `smartclock-0.2.0-firmware.bin` 을 올립니다. "완료" 후 시계가 재시작합니다.
3. 재시작 후 화면이 디자인 C(좌우 반원 링)로 나오는지 봅니다.
4. 설정 페이지 맨 위 `Firmware` 가 `0.2.0` 인지 확인합니다.
5. 필요하면 `Display` 묶음에서 디자인·자동 순환을 고르고 저장합니다(8.2절).

0.1.1(LCD 수정판)에서 바로 올리는 경우, 검증 방식은 기존 설정에서 자동으로 정해집니다. 0.1.3 에서 올리는 경우는 0.1.3 의 검증 방식이 그대로 남습니다.

| 0.1.1 설정 | 0.2.0 검증 방식 |
|---|---|
| fingerprint 입력돼 있음 | `fingerprint` |
| fingerprint 비어 있음 (0.1.1 에서는 검증 안 함이었음) | `CA` |

> **CAUTION** — 0.1.1 에서 fingerprint 를 비우고 Let's Encrypt 가 아닌 HTTPS 서버에 붙어 있었다면 0.2.0 에서는 `CA` 방식이 되어 조회가 실패합니다(`connection failed (TLS …)`). fingerprint 를 넣거나 nginx 인증서를 Let's Encrypt 로 바꿉니다. `http://` 주소는 영향이 없습니다.

- 사용량 표시를 처음 연결하려면 `server/README.ko.md` 6장 순서로 URL·토큰·`CA` 를 넣습니다.

#### 되돌리기 (0.2.0 → 0.1.3)

1. Releases `hardware-v0.1.3` 의 `smartclock-0.1.3-firmware.bin` 을 같은 `/update` 페이지에 올립니다.
2. Wi-Fi·비밀번호·URL·토큰·검증 방식은 그대로 남습니다. 화면은 0.1.3 의 막대 화면으로 돌아갑니다.

- 0.1.3 은 디자인 설정 키(`layout`·`layout_mode`·`cycle_sec`)를 읽지 않습니다. **0.1.3 에서 설정을 저장하면 이 세 키가 파일에서 빠집니다.** 그 뒤 다시 0.2.0 을 올리면 디자인은 기본값(`C`·`fixed`·15초)으로 돌아갑니다. 0.1.3 에서 저장하지 않았으면 이전 디자인 설정이 그대로 살아납니다.
- 0.1.2 로는 되돌리지 않습니다(0장 CAUTION).

## 10. 사용량 JSON 계약

계약 원문은 [`../server/README.ko.md`](../server/README.ko.md) 2장입니다. 계약을 바꾸면 두 문서를 함께 고칩니다. 펌웨어 쪽 처리는 다음과 같습니다.

요청 헤더는 `Accept: application/json` 이고, 토큰이 있으면 `Authorization: Bearer <token>` 을 붙입니다. 본문은 4,096 바이트 이하여야 하고, **리다이렉트는 따라가지 않습니다.** HTTP 200 이 아니면 조회 실패입니다.

| 필드 | 펌웨어 처리 |
|---|---|
| `items` | 필수. 0.2.0 은 앞 2개를 게이지로 표시합니다(`items[0]` Claude, `items[1]` Fable). 3번째부터는 표시하지 않습니다 |
| `items[].label` | 게이지 이름으로 표시. 16바이트를 넘으면 잘림. ASCII 권장 |
| `items[].value` | 0.2.0 화면에는 표시하지 않습니다. 게이지 숫자는 `pct` 로 씁니다 |
| `items[].pct` | 숫자면 게이지와 `N%` (70 미만 초록, 70–89 노랑, 90 이상 빨강). `null` 이면 회색 트랙과 `--` |
| `items[].resets` | `MM-DD HH:MM` 이면 맨 아래 줄에 `초기화 11-18 09:00` (8.2절 우선순위 표) |
| `updated` | 상태 문구 `HH:MM 기준`. `null` 이면 `N분 전` |
| `status` / `error` | 아래 상태 문구 표 |

상태 문구 (위에서부터 먼저 맞는 줄 하나). 0.1.x 는 머리글 오른쪽에, 0.2.0 은 맨 아래 줄에 8.2절 우선순위에 따라 표시합니다.

| 상황 | 표시 | 색 |
|---|---|---|
| URL 미설정 | `URL 미설정` | 회색 |
| CA 방식인데 시각 미동기화 | `시각 동기화 대기` | 회색 |
| 아직 한 번도 받지 못함 | `불러오는 중` / 실패 시 `실패` | 회색 / 주황 |
| 시계 → 중계 서버 조회 실패 | `실패 · N분 전` | 주황 |
| `status:"stale"` | `지연 · HH:MM 기준` | 주황 |
| `status:"error"` | `서버 오류 <error 앞 12자>` | 주황 |
| 정상 | `HH:MM 기준` | 회색 |

- 0.1.x 막대 화면에서는 주황색 문구가 나오면 제목("최근 7일 사용량")을 숨기고 문구만 표시합니다.

## 11. 문제 해결

| 증상 | 원인·조치 |
|---|---|
| 부팅 직후 `Exception (9)` 반복, 화면 없음 | 0.1.0 또는 0.1.2 입니다. 0.2.0(또는 0.1.3)을 시리얼로 기록합니다 |
| `Failed to connect` (esptool) | 5장 끝 참조. TX·RX 교차, GPIO0–GND, 전원 재투입 |
| 설정 페이지 `failed: HTTP 401` | 토큰 불일치 |
| `failed: HTTP 429` | 토큰을 여러 번 틀려 중계 서버가 15분 차단, 또는 nginx 요청 수 제한. `server/README.ko.md` 7장 |
| `failed: HTTP 301` / `302` | URL 이 `http://` 인데 nginx 가 HTTPS 로 돌려보냄. URL 을 `https://` 로 |
| `failed: HTTP 404` | 경로 오타. 정확히 `/clock/usage` |
| `failed: connection failed (TLS …)` | 인증서 검증 실패 또는 메모리 부족. `server/README.ko.md` 5.6절 체인 확인, 8.1절 `tls` 값 확인 |
| 머리글 `시각 동기화 대기` 가 계속 | NTP(UDP 123) 가 막힌 망. 공유기 설정 확인 |
| 화면 색이 이상 | `LCD_BGR` / `LCD_INVERT` 반전 후 재빌드 |
| 화면 디자인이 저절로 바뀜 | `Design change` 가 `cycle` 입니다. `fixed` 로 바꾸고 저장합니다(8.2절) |
| 설정 저장 시 `Cycle interval must be 5-300 seconds.` | 순환 간격은 5–300초만 받습니다 |
| Fable 게이지가 계속 회색 `--` | 중계 서버가 `items[1].pct` 를 `null` 로 줍니다. 계정에 Fable 주간 한도 행이 없는 경우입니다(`server/README.ko.md` 1.4절) |
| 0.1.3 으로 되돌렸다가 0.2.0 을 다시 올렸더니 디자인이 C 로 돌아감 | 0.1.3 에서 설정을 저장하면 디자인 설정이 지워집니다(9.3절 되돌리기). 다시 고르고 저장합니다 |
| 업로드 후 `Firmware` 가 0.2.0 이 아님 | 업로드가 끝나기 전에 끊겼습니다. 같은 파일을 다시 올립니다. 부팅이 안 되면 6장(시리얼) |

## 12. 파일 구성

| 경로 | 내용 |
|---|---|
| `platformio.ini` | env 3개: `smartclock`, `smartclock-ota`, `lcd-diag` |
| `src/main.cpp` | 부팅 순서, Wi-Fi, NTP, ArduinoOTA, 주 루프 |
| `src/lcd.cpp` | ST7789 드라이버, 글꼴 렌더러, 안티앨리어싱 원호(`drawRing`) |
| `src/ui.cpp` | 화면 디자인 A·B·C 배치와 부분 갱신 |
| `include/layout_cycle.h` | 고정/자동 순환 디자인 선택 |
| `src/web.cpp` | 설정 포털, 관리 페이지, `/update` |
| `src/usage.cpp` / `src/usage_model.cpp` | 사용량 조회(HTTP/HTTPS) / JSON 해석·머리글 문구 |
| `src/settings.cpp` / `src/settings_model.cpp` | 설정 저장 / 설정 변환·검증 방식·경고 판정 |
| `src/ca_roots_gen.cpp`, `include/ca_roots.h` | 내장 루트 인증서 |
| `src/diag.cpp` | 화면 핀 진단 |
| `src/fonts_gen.cpp` | 생성된 글꼴 표 |
| `tools/clean_flash.sh`, `tools/verify_flash.py` | 지우기·기록·검증 |
| `tools/gen_fonts.py`, `tools/gen_ca_roots.py`, `tools/glyphs_ko.txt` | 글꼴·루트 인증서 생성 |
| `test/pc/` | PC 시험 (`run.sh`, `test_usage.cpp`, Arduino API 대체 헤더) |
| `test/pc/sim/` | 화면 PC 렌더러(가짜 ST7789), 부분 갱신·겹침·순환 검사, PNG 비교표 스크립트 |
