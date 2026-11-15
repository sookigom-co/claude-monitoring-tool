# SmartClock — SD Pro Smart Clock Conversion

**[English](#english)** | [한국어](#한국어)

## English

This project wipes the stock firmware of the SD Pro smart clock (ESP8266) and installs our own firmware, which shows the time together with **weekly Claude and Fable usage** (ring gauges, 3 screen designs).

| Folder | Contents | Current version | Manual |
|---|---|---|---|
| [`hardware/`](hardware/) | Clock firmware (PlatformIO · Arduino ESP8266) | **0.2.0** | [English](hardware/README.md) · [한국어](hardware/README.ko.md) |
| [`server/`](server/) | Usage relay server `clock-usage-relay` (Python standard library) + front nginx config | **1.2.0** | [English](server/README.md) · [한국어](server/README.ko.md) |

```
                      HTTPS 443                            HTTP 8787                         HTTPS
┌──────────────────┐                ┌───────────────────┐                ┌────────────────┐            ┌───────────────────┐
│ Clock (hardware) │ ─────────────▶ │ Front nginx proxy │ ─────────────▶ │ relay (server) │ ─────────▶ │ api.anthropic.com │
│ ESP8266          │  Bearer token  │ TLS termination,  │  internal      │ token check,   │  once per  │                   │
│                  │                │ path restriction  │  network only  │ cache          │  5 min     │                   │
└──────────────────┘                └───────────────────┘                └────────────────┘            └───────────────────┘
```

- The clock calls only one relay endpoint, `GET /clock/usage`. The contract is in Section 2 of the server manual.
- HTTPS is handled by a **separate nginx proxy server that is already in operation**. The relay listens on plain HTTP on the internal network only and accepts connections only from the nginx server IP.
- The Anthropic OAuth token stays on the relay server only. Only usage numbers and the relay token pass through the clock and nginx.

### First-time installation order

1. **Install the relay** — server manual, Section 4 (relay server)
2. **Connect nginx and publish over HTTPS** — server manual, Section 5 (nginx server + relay server firewall)
3. **Flash the clock firmware** — hardware manual, Sections 4–7 (the first flash requires a USB-serial adapter). If the clock already runs our firmware (0.1.1 LCD-fix build or 0.1.3), use Section 9.3 (over-the-air update to 0.2.0)
4. **Enter the URL and token on the clock** — server manual, Section 6

Manuals: hardware [English](hardware/README.md) · [한국어](hardware/README.ko.md), server [English](server/README.md) · [한국어](server/README.ko.md).

### Built files

Binaries are published on GitHub Releases. The repository does not contain binaries.

| Release | Files |
|---|---|
| `hardware-v0.2.0` (latest) | `smartclock-0.2.0-firmware.bin`, `lcd-diag-0.2.0-firmware.bin`, `SHA256SUMS` |
| `hardware-v0.1.3` | `smartclock-0.1.3-firmware.bin`, `lcd-diag-0.1.3-firmware.bin`, `SHA256SUMS` (for rollback) |
| `server-v1.2.0` | `clock-usage-relay-1.2.0.tar.gz`, `SHA256SUMS` |

### History

| Task | Description |
|---|---|
| SOO-596 | Stock firmware analysis, custom firmware feasibility review, flashing equipment guide |
| SOO-597 | Firmware 0.1.0, then a fix for the LCD transfer alignment defect (0.1.1 LCD-fix build) |
| SOO-598 | relay 1.0.0 |
| SOO-599 | Firmware 0.1.1 (relay contract applied) |
| SOO-600 | Firmware 0.1.2 (HTTPS CA verification), relay 1.1.0 (Caddy publishing procedure) |
| SOO-612 | This repository created. hardware / server split, firmware 0.1.3 (0.1.2 + LCD fix merged), relay 1.2.0 (front nginx setup) |
| SOO-618 | Screen layout mockups — time and date + 2 ring gauges, design A (concentric rings) · B (clock on top + gauges below) · C (left/right half rings) |
| SOO-619 | Firmware 0.2.0 — 3 screen designs with fixed/auto-cycle setting, ring gauges, redraw only changed areas |
| SOO-621 | Firmware 0.2.0 added to this repository, Release `hardware-v0.2.0` |
| SOO-623 | English documentation: README in English and Korean, hardware/server manuals split into English (`README.md`) and Korean (`README.ko.md`) |

### Secrets

This repository contains no tokens, credentials, or Wi-Fi passwords. `.gitignore` blocks `credentials.json`, `clock_token`, `.env`-type files, and flash dumps.

---

## 한국어

SD Pro 스마트 클락(ESP8266)의 순정 펌웨어를 지우고 자체 펌웨어를 올려, 시계와 **Claude·Fable 주간 사용률**(원형 게이지, 화면 디자인 3종)을 함께 표시하는 프로젝트입니다.

| 폴더 | 내용 | 현재 버전 | 사용 설명서 |
|---|---|---|---|
| [`hardware/`](hardware/) | 시계 펌웨어 (PlatformIO · Arduino ESP8266) | **0.2.0** | [한국어](hardware/README.ko.md) · [English](hardware/README.md) |
| [`server/`](server/) | 사용량 중계 서버 `clock-usage-relay` (Python 표준 라이브러리) + 앞단 nginx 설정 | **1.2.0** | [한국어](server/README.ko.md) · [English](server/README.md) |

```
┌──────────────┐  HTTPS 443   ┌───────────────────┐  HTTP 8787   ┌──────────────┐  HTTPS   ┌───────────────────┐
│ 시계 (hardware)│ ───────────▶ │ 앞단 nginx 프록시 서버 │ ───────────▶ │ relay (server) │ ───────▶ │ api.anthropic.com │
│ ESP8266       │  Bearer 토큰  │ TLS 종료·경로 제한   │  내부망 전용   │ 토큰 확인·캐시   │  5분 1회  │                   │
└──────────────┘              └───────────────────┘              └──────────────┘          └───────────────────┘
```

- 시계는 relay 의 `GET /clock/usage` 하나만 호출합니다. 계약은 `server/README.ko.md` 2장입니다.
- HTTPS 는 **기존에 운영 중인 별도 nginx 프록시 서버**가 처리합니다. relay 는 내부망에서 평문 HTTP 로만 열리고, nginx 서버 IP 에서만 접속을 받습니다.
- Anthropic OAuth 토큰은 relay 서버 안에만 있습니다. 시계와 nginx 에는 사용률 숫자와 relay 토큰만 지나갑니다.

### 처음 설치 순서

1. **relay 설치** — `server/README.ko.md` 4장 (relay 서버)
2. **nginx 연결·HTTPS 공개** — `server/README.ko.md` 5장 (nginx 서버 + relay 서버 방화벽)
3. **시계 펌웨어 기록** — `hardware/README.ko.md` 4~7장 (처음 한 번은 USB-시리얼 어댑터로 기록), 이미 우리 펌웨어(0.1.1 LCD 수정판·0.1.3)가 있으면 9.3절(무선 업데이트로 0.2.0)
4. **시계에 URL·토큰 입력** — `server/README.ko.md` 6장

### 빌드된 파일

GitHub Releases 에 올립니다. 저장소에는 바이너리를 넣지 않습니다.

| 릴리스 | 파일 |
|---|---|
| `hardware-v0.2.0` (최신) | `smartclock-0.2.0-firmware.bin`, `lcd-diag-0.2.0-firmware.bin`, `SHA256SUMS` |
| `hardware-v0.1.3` | `smartclock-0.1.3-firmware.bin`, `lcd-diag-0.1.3-firmware.bin`, `SHA256SUMS` (되돌리기용) |
| `server-v1.2.0` | `clock-usage-relay-1.2.0.tar.gz`, `SHA256SUMS` |

### 이력

| 태스크 | 내용 |
|---|---|
| SOO-596 | 순정 펌웨어 분석, 커스텀 펌웨어 가능성 검토, 기록 장비 안내 |
| SOO-597 | 펌웨어 0.1.0, 이후 LCD 전송 정렬 결함 수정(0.1.1 LCD 수정판) |
| SOO-598 | relay 1.0.0 |
| SOO-599 | 펌웨어 0.1.1 (relay 계약 반영) |
| SOO-600 | 펌웨어 0.1.2 (HTTPS CA 검증), relay 1.1.0 (Caddy 공개 절차) |
| SOO-612 | 이 저장소 생성. hardware / server 분리, 펌웨어 0.1.3(0.1.2 + LCD 수정 통합), relay 1.2.0(앞단 nginx 구성) |
| SOO-618 | 화면 레이아웃 시안 — 시각·날짜 + 원형 게이지 2개, 디자인 A(동심원 링) · B(위 시계 + 아래 게이지) · C(좌우 반원 링) |
| SOO-619 | 펌웨어 0.2.0 — 화면 디자인 3종과 고정/자동 순환 설정, 원형 게이지, 바뀐 부분만 다시 그리기 |
| SOO-621 | 펌웨어 0.2.0 을 이 저장소에 반영, Release `hardware-v0.2.0` |
| SOO-623 | 문서 영문화 — README 영어·한국어 병기, hardware/server 설명서를 영어(`README.md`)·한국어(`README.ko.md`)로 분리 |

### 비밀값

이 저장소에는 토큰·자격증명·Wi-Fi 비밀번호를 넣지 않습니다. `.gitignore` 가 `credentials.json`, `clock_token`, `.env` 류와 flash 덤프를 막습니다.
