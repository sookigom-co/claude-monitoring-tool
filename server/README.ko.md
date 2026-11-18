[English](README.md) | **한국어**

# clock-usage-relay (server) — 1.3.0

스마트 클락이 화면 아래에 표시할 **Claude 구독 주간 한도 사용률(%)** 을 Anthropic 에서 받아 작은 JSON 으로 내려주는 중계 서버입니다. Python 3.9 이상 표준 라이브러리만 씁니다(외부 패키지 0개).

1.3.0 부터 relay 는 시계 요청과 상관없이 **5분마다 사용률을 표본으로 저장**합니다(`sqlite3`, 14일 보존). 시계 요청에는 저장된 최신 표본으로 바로 답하고, 표본 차이로 계산한 **증가값**(최근 1시간·24시간, 정시 1시간 구간별)을 함께 내려줍니다. 계산 규칙은 2.1절입니다.

시계 펌웨어는 [`../hardware/README.ko.md`](../hardware/README.ko.md) 에 있습니다.

```
[시계 ESP8266] --HTTPS--> [앞단 nginx 프록시 서버] --HTTP(내부망)--> [relay :8787] --5분에 1회--> api.anthropic.com
                 Bearer relay 토큰      TLS 종료·경로 제한·요청 수 제한         토큰 확인·표본 저장·OAuth 갱신
```

Anthropic OAuth 토큰은 relay 서버 안에만 있습니다. 시계에는 사용률 숫자만 내려갑니다.

## 0. 검증 상태

| 항목 | 상태 |
|---|---|
| 단위 시험 (`tests/test_relay.py`) | `CONFIRMED` — 60건 통과 (2026-11-18) |
| nginx 연동 시험 (`tests/integration/run_nginx_it.sh`) | `CONFIRMED` — nginx 1.26.3(Debian 13 패키지 `1.26.3-3+deb13u7`) + 동봉 설정 파일로 12건 통과 (2026-11-18). 10.2절 |
| 운영 서버 `python3` 에 `sqlite3` 모듈 있음 | `CONFIRMED` — 운영 relay 서버에서 `sqlite3` 3.46.1 확인(2026-11-18, 1.3.0 업그레이드 시) |
| 상위 `utilization` 에 소수점이 있음 | `UNVERIFIED` — relay 는 값을 소수 그대로 저장합니다. 응답의 `pct_raw` 로 확인합니다 |
| 주간 한도가 고정 창이고 `resets_at` 에 0 으로 돌아감 | `CONFIRMED` (계정 소유자 진술, 2026-11-18) — 매주 일요일 15:00 KST(06:00 UTC)에 초기화됩니다. 증가값 계산은 이 전제를 씁니다(2.1절). 저장된 표본과의 대조는 아직 `UNVERIFIED` 입니다 |
| 상위가 하루 288회 조회를 막지 않음 | `SUSPECT` — 3장 CAUTION |
| 실제 Anthropic 응답 형식, Fable 행 유무 | `UNVERIFIED` — 설치 후 첫 호출에서 확인합니다(4.5절) |
| 실제 도메인·Let's Encrypt 인증서로 시계 접속 | `UNVERIFIED` |

## 1. 데이터 출처 조사 결과

### 1.1 조사 방법과 한계

- 조사 대상: 컨테이너에 설치된 Claude Code **2.1.319** 실행 파일 안의 사용량 조회·토큰 갱신 코드(문자열·스키마 정의). 2026-11-13 확인.
- 실제 토큰으로 엔드포인트를 호출하지 **않았습니다.** 보드 계정 토큰을 받지 않는다는 태스크 조건 때문입니다. 따라서 아래 응답 형식은 "Claude Code 가 이렇게 읽는다"는 근거이고, 실제 응답 확인은 설치 후 첫 호출에서 합니다. → 전 항목 `UNVERIFIED (실호출 미확인)`.
- 공식 공개 API 가 아닙니다. Anthropic 이 예고 없이 형식을 바꾸거나 막을 수 있습니다.

### 1.2 사용량 조회 엔드포인트

| 항목 | 값 | 근거 |
|---|---|---|
| URL | `GET https://api.anthropic.com/api/oauth/usage` | 클라이언트 코드 경로 문자열 (`/api/oauth/usage`, 기본 base `https://api.anthropic.com`) |
| 인증 | `Authorization: Bearer <OAuth access token>` | 같은 코드 |
| 헤더 | `anthropic-beta: oauth-2025-04-20` | OAuth 요청 공통 베타 상수 |
| 필요 scope | `user:profile` 포함 토큰 | 클라이언트 설명문 "null when … a token without the profile scope" |
| 선택 쿼리 | `?skip_spend=1` 등 | relay 는 쿼리 없이 호출 |

### 1.3 응답 필드 (Claude Code 가 파싱하는 스키마 기준)

| 필드 | 형식 | 의미 | relay 사용 |
|---|---|---|---|
| `five_hour` | `{utilization: number\|null, resets_at: ISO8601\|null}` | 5시간 세션 한도 | 사용 안 함 |
| `seven_day` | 위와 같음 | **주간 전체 한도** | **Claude 칸** |
| `seven_day_opus` | 위와 같음 | 주간 Opus 한도 | 사용 안 함 |
| `seven_day_sonnet` | 위와 같음 | 주간 Sonnet 한도 | 사용 안 함 |
| `seven_day_oauth_apps` | 위와 같음 | 주간 OAuth 앱 한도 | 사용 안 함 |
| `cinder_cove`, `wattle_ember` | 위와 같음(+금액 필드) | 내부 코드명, 의미 미확인 | 사용 안 함 |
| `extra_usage` | `{is_enabled, monthly_limit, used_credits, utilization, currency}` | 추가 사용(초과 과금) | 사용 안 함 |
| `limits[]` | `{kind, group, percent, resets_at, severity, is_active, scope:{model:{display_name}}}` | 서버가 정한 표시용 행 목록 | **Fable 칸**, Claude 칸 예비 |

- `utilization` 단위: 클라이언트 설명문 기준 **0~100 (%)**. 응답 헤더(`anthropic-ratelimit-unified-*`)는 0~1 이지만 relay 는 헤더를 쓰지 않습니다.
- `limits[].kind` 예: `session`, `weekly_all`, `weekly_scoped`. 모델별 주간 행은 `kind="weekly_scoped"` 이고 `scope.model.display_name` 에 모델 표시명이 옵니다. 클라이언트 설명문의 예시가 정확히 `'Fable'` 입니다.

### 1.4 Fable 버킷 유무

| 경로 | 상태 | 근거 |
|---|---|---|
| `limits[]` 의 `kind="weekly_scoped"` + `scope.model.display_name="Fable"` | **있음** (`SUSPECT` — 계정에 따라 있을 수도 없을 수도) | 설명문 "Per-model weekly windows from the server limits[] array" · 예시 `'Fable'` |
| 버킷 키 `seven_day_overage_included` | 있음 (주로 응답 **헤더** 쪽) | 클라이언트 라벨표 `seven_day_overage_included: "Fable limit"`, 리셋 이름 `"Fable reset"` |
| `seven_day_fable` 같은 이름의 키 | **없음** | 실행 파일 전체에 해당 문자열 0건 |

relay 의 Fable 칸 결정 순서:
1. `limits[]` 에서 `weekly_scoped` 이고 모델명에 `fable` 이 들어간 행의 `percent`.
2. 없으면 최상위 `seven_day_overage_included.utilization`.
3. 둘 다 없으면 `{"pct":null,"value":"n/a"}`. 다른 값으로 채우지 않습니다.

### 1.5 토큰 만료·갱신

| 항목 | 값 | 근거 |
|---|---|---|
| 갱신 URL | `POST https://platform.claude.com/v1/oauth/token` | 클라이언트 `TOKEN_URL` |
| 본문 | JSON `{"grant_type":"refresh_token","refresh_token":…,"client_id":"9d1c250a-e61b-44d9-88ed-5944d1962f5e","scope":"user:profile user:inference …"}` | 같은 코드 |
| 응답 | `{access_token, refresh_token, expires_in, scope}` | 같은 코드 |
| refresh token 회전 | **응답마다 새 refresh token 이 올 수 있음.** 없으면 기존 값 유지 | 클라이언트가 `refresh_token = 응답값 ?? 기존값` 으로 처리 |
| 저장 형식 | `~/.claude/.credentials.json` 의 `claudeAiOauth.{accessToken, refreshToken, expiresAt(ms)}` | Claude Code 저장 형식 |

relay 동작:
- 만료 5분 전이면 미리 갱신합니다. 사용량 조회가 401/403 이면 1회 갱신 후 재시도합니다.
- 갱신 결과(새 access/refresh token)를 자격증명 파일에 **원자적으로 다시 씁니다**(임시 파일 → `rename`, 권한 600).
- 갱신이 실패하면 표본을 저장하지 않고, 마지막 표본을 그대로 내려주며 `error:"refresh_failed"` 를 붙입니다. 마지막 표본이 표본 간격 × 2 보다 오래되면 `status:"stale"` 이 됩니다(2장).

> **CAUTION** — refresh token 이 회전하므로 **같은 로그인 정보를 relay 와 다른 Claude Code 가 함께 쓰면 한쪽이 로그아웃됩니다.** relay 전용으로 새로 로그인한 자격증명을 쓰십시오(4.3절).

> **NOTE** — `claude setup-token` 으로 만드는 장기 토큰은 scope 가 `user:inference` 하나뿐입니다. 사용량 조회에 필요한 `user:profile` 이 없어 **동작하지 않을 것으로 봅니다** (`SUSPECT`).

---

## 2. JSON 계약 (확정본)

`GET /clock/usage` · 헤더 `Authorization: Bearer <relay 토큰>`

```json
{"updated":"2026-11-18T14:25:00+09:00","period":"7d","status":"ok","version":"1.3.0","sample_age_s":120,
 "items":[{"label":"Claude","value":"42%","pct":42,"resets":"11-18 09:00","pct_raw":42.3,"delta_1h":1.4,"delta_24h":11.2},
          {"label":"Fable","value":"17%","pct":17,"resets":"11-18 09:00","pct_raw":17.0,"delta_1h":0.0,"delta_24h":2.0}]}
```

`GET /clock/usage?hourly=1` 이면 항목마다 `hourly`(24개)가 붙습니다.

```json
{"label":"Claude", …, "delta_24h":11.2,"hourly":[0.0,null,0.3,1.1, … ,0.4]}
```

| 필드 | 형식 | 설명 |
|---|---|---|
| `updated` | ISO8601 문자열 \| `null` | 최신 표본 시각 = 마지막 **성공** 조회 시각(`RELAY_TZ`, 기본 KST). 표본이 없으면 `null` |
| `period` | `"7d"` | 고정 |
| `items[]` | 항상 2개, 순서 `Claude`, `Fable` | |
| `items[].value` | 문자열 | `"42%"` 또는 `"n/a"` |
| `items[].pct` | 0~100 정수 \| `null` | 반올림·범위 제한 적용 |
| `items[].resets` | `"MM-DD HH:MM"` (선택) | 주간 한도 초기화 시각. 모르면 필드 자체가 없음 |
| `status` | `"ok"` \| `"stale"` \| `"error"` | **추가 필드.** 최신 표본 나이로 정합니다. `ok` = 표본 간격 × 2 이내(기본 600초). `stale` = 그보다 오래됨, 마지막 표본 값을 표시 중. `error` = 표본이 하나도 없음 |
| `error` | 문자열 (선택) | **추가 필드.** 가장 최근 상위 호출이 실패했으면 그 코드. `refresh_failed`, `usage_http_401`, `network`, `credentials_unreadable`, `internal`, `storage_error`(표본 파일 읽기·쓰기 실패), `no_sample`(기동 직후 첫 표본 전). `status` 가 `ok` 여도 붙을 수 있습니다(한 번 실패했지만 표본은 아직 새것) |
| `version` | 문자열 | **1.3.0 추가.** relay 버전 |
| `sample_age_s` | 정수 \| `null` | **1.3.0 추가.** 최신 표본 나이(초) |
| `items[].pct_raw` | 실수 \| `null` | **1.3.0 추가.** 상위 원값(반올림·범위 제한 없음) |
| `items[].delta_1h` | 실수 \| `null` | **1.3.0 추가.** 최근 약 1시간 증가값(%p, 소수 1자리). 2.1절 |
| `items[].delta_24h` | 실수 \| `null` | **1.3.0 추가.** 최근 약 24시간 증가값(%p, 소수 1자리) |
| `items[].hourly` | 실수 \| `null` 24개 (선택) | **1.3.0 추가, `?hourly=1` 일 때만.** 최근 **완료된** 정시 구간 24개의 증가값, 오래된 것부터. 마지막 값이 직전 정시 구간(14:25 에 조회하면 13:00~14:00) |

- 1.2.0 필드(`updated`·`period`·`status`·`items[].label/value/pct/resets`)의 형식·의미는 1.3.0 에서 바뀌지 않았습니다. `pct` 는 계속 정수입니다.
- 기본 응답은 약 340바이트, `?hourly=1` 응답은 약 550~600바이트입니다(목 서버 실측). 시계 응답 한도 4,096바이트 미만을 시험으로 고정합니다. 펌웨어 0.3.0 은 `?hourly=1` 을 쓰지 않습니다.
- `status`·`error` 는 SOO-597 초안 대비 **추가**입니다. 펌웨어는 모르는 필드를 무시하면 됩니다. 펌웨어 0.1.1 이후는 `status` 를 머리글에 표시합니다(`hardware/README.ko.md` 10장).
- 기타 응답: 토큰 없음·불일치 `401 {"error":"unauthorized"}`, 토큰 오입력 반복으로 차단된 접속 주소 `429 {"error":"too_many_failures"}` + `Retry-After`(초), 다른 경로 `404`, GET 외 메서드 `405`, `GET /healthz` 는 인증 없이 `{"ok":true,"version":"1.3.0","samples":288,"latest_sample":"2026-11-18T14:25:00+09:00"}`(표본 수·최신 표본 시각만, 사용률 값 없음. 표본 파일을 못 읽으면 `samples`·`latest_sample` 이 `null`. 5장 nginx 구성에서는 외부에 공개하지 않습니다).

### 2.1 증가값 계산

- **표본.** relay 는 기동 직후 1회, 이후 `RELAY_SAMPLE_SECONDS`(기본 300초)마다 상위를 호출해 Claude·Fable 값과 각 `resets_at` 을 1행으로 저장합니다. 실패한 호출은 행을 남기지 않습니다(빈 구간). 서버가 꺼진 시간도 빈 구간이고 0 으로 채우지 않습니다.
- **`delta_1h`·`delta_24h`.** 최신 표본 − (최신 표본 시각 − 60분/24시간 이전의 가장 최근 표본). 표본이 5분 간격이므로 실제 구간은 60분보다 **최대 5분 깁니다**(24시간도 같음). 비교 표본이 없거나 그 표본이 기준 시각보다 표본 간격 × 2 이상 오래됐으면(서버가 꺼져 있었음) `null` 입니다.
- **`hourly`.** `RELAY_TZ` 기준 정시 경계 구간. 구간 h 의 값 = (h 의 마지막 표본) − (h−1 의 마지막 표본). 어느 한쪽 구간에 표본이 없으면 `null`.
- **초기화 보정.** 비교하는 두 표본 사이에서 `resets_at` 이 1시간 이상 바뀌면 주간 한도가 초기화된 것으로 보고, 증가값 = (초기화 직전 마지막 표본 − 시작 표본) + (끝 표본 − 0) 으로 계산합니다. 초기화 직전 표본이 이전 `resets_at` 시각보다 표본 간격 × 2 이상 앞서면(초기화 무렵 표본 없음) `null` 입니다. 1시간 미만의 `resets_at` 흔들림은 초기화로 보지 않습니다.
- **감소.** `resets_at` 이 그대로인데 값이 줄면 음수를 그대로 돌려줍니다(0 으로 자르지 않음). 주간 한도가 이동 창인지 확인하는 데 씁니다.
- **반올림.** 저장은 상위 값 그대로(소수), 응답 직전에만 소수 1자리로 반올림합니다. 상위가 정수만 주면 `1.0` 처럼 나옵니다.


---

## 3. 설정값

`/etc/clock-usage-relay/relay.env` 에 넣습니다(예시: `deploy/relay.env.example`).

| 변수 | 기본값 | 설명 |
|---|---|---|
| `RELAY_BIND` | `127.0.0.1` | 바인드 주소. **nginx 가 다른 서버에 있으면** relay 서버의 내부망 IP 또는 `0.0.0.0` 으로 바꿉니다(5.3절) |
| `RELAY_PORT` | `8787` | 포트 |
| `RELAY_TRUSTED_PROXIES` | `127.0.0.1,::1` | nginx 서버 주소. 쉼표로 여러 개, CIDR 가능(`10.0.0.12`, `10.0.0.0/24`). 이 주소에서 온 요청만 `X-Forwarded-For` 의 마지막 값을 실제 접속 주소로 봅니다. 잘못된 값이면 기동하지 않습니다 |
| `RELAY_SAMPLE_SECONDS` | `300` | 표본 간격 = 상위 호출 간격(최소 60). 실패해도 이 간격을 지킵니다. 없으면 1.2.0 의 `RELAY_CACHE_SECONDS` 값을 씁니다 |
| `RELAY_SAMPLE_RETENTION_DAYS` | `14` | 표본 보존 일수(최소 2). 표본을 저장할 때마다 오래된 행을 지웁니다. 14일 ≈ 4,032행, 1MB 미만 |
| `RELAY_SAMPLES_DB` | `$STATE_DIRECTORY/samples.db` | 표본 파일(`sqlite3`). systemd 아래에서는 `/var/lib/clock-usage-relay/samples.db`. 파일 권한 600, 폴더가 없으면 700 으로 만듭니다 |
| `RELAY_TZ` | `Asia/Seoul` | 표시 시간대 |
| `RELAY_CREDENTIALS_FILE` | (필수) | Claude Code `.credentials.json` 형식 파일. 갱신 시 relay 가 다시 씁니다 |
| `RELAY_CLOCK_TOKEN_FILE` | (필수, 또는 `RELAY_CLOCK_TOKEN`) | 시계 인증 토큰. 16자 이상 |
| `RELAY_AUTH_MAX_FAILURES` | `10` | 한 접속 주소가 구간 안에 토큰을 이만큼 틀리면 차단 |
| `RELAY_AUTH_WINDOW_SECONDS` | `600` | 실패 횟수를 세는 구간(초) |
| `RELAY_AUTH_LOCKOUT_SECONDS` | `900` | 차단 시간(초). 차단 중에는 맞는 토큰도 `429` |
| `RELAY_USAGE_URL` · `RELAY_TOKEN_URL` · `RELAY_OAUTH_CLIENT_ID` · `RELAY_OAUTH_SCOPES` · `RELAY_OAUTH_BETA` | 1장 표의 값 | 상위 형식이 바뀌었을 때 코드 수정 없이 바꾸는 용도 |

> **CAUTION** — 1.3.0 은 시계 수와 상관없이 상위를 **하루 최대 288회**(5분마다) 호출합니다. 1.2.0 은 최대 144회였습니다. 상위가 이 빈도를 허용하는지는 `SUSPECT` 입니다. 로그에 `usage_http_429` 등 차단이 보이면 `RELAY_SAMPLE_SECONDS=600` 으로 늘리고 재시작합니다.

> **CAUTION** — nginx 가 다른 서버에 있는데 `RELAY_TRUSTED_PROXIES` 에 그 주소를 넣지 않으면, relay 는 모든 시계를 nginx 주소 하나로 봅니다. 그러면 누군가 토큰을 10번 틀리는 것만으로 **모든 시계가 15분간 차단**됩니다. 이 상태로 기동하면 로그에 `RELAY_TRUSTED_PROXIES has only loopback` 경고가 남습니다.

---

## 4. relay 설치

> **WARNING** — 이 절차는 운영 서버에서 root 로 실행하는 명령입니다. 보드 또는 보드가 지시한 담당자가 직접 실행합니다. 에이전트는 `ssh-rw` 로 실행하지 않습니다(회사 규칙 SOO-184).

### 4.1 설치 위치

- 하이퍼바이저(Proxmox) 호스트에 직접 두지 말고 Debian 13 LXC 컨테이너 1개(메모리 128~256MB, 디스크 1GB)에 설치하기를 권장합니다.
- 요구사항: `python3` 3.9 이상, systemd, 바깥으로 HTTPS(443) 접속 가능.
- nginx 프록시 서버와 같은 서버에 둘 수도 있습니다. 그 경우 `RELAY_BIND`·`RELAY_TRUSTED_PROXIES` 는 기본값 그대로 두고 nginx 설정의 `RELAY_HOST` 를 `127.0.0.1` 로 씁니다.

### 4.2 파일 설치

레포를 받은 뒤 `server/` 폴더에서 실행합니다.

```bash
python3 --version                                  # 3.9 이상
useradd --system --no-create-home --shell /usr/sbin/nologin clockrelay
install -d -m 755 /opt/clock-usage-relay
install -m 644 clock_usage_relay.py /opt/clock-usage-relay/
install -d -m 750 -o root -g clockrelay /etc/clock-usage-relay
install -m 640 -o root -g clockrelay deploy/relay.env.example /etc/clock-usage-relay/relay.env
install -d -m 700 -o clockrelay -g clockrelay /var/lib/clock-usage-relay
install -m 644 deploy/clock-usage-relay.service /etc/systemd/system/
```

그다음 `/etc/clock-usage-relay/relay.env` 의 `NGINX_PROXY_IP` 를 nginx 서버의 내부망 IP 로 바꿉니다(5.3절). nginx 와 같은 서버면 `RELAY_BIND=127.0.0.1` 로 바꾸고 `RELAY_TRUSTED_PROXIES` 줄을 지웁니다.

### 4.3 비밀값 넣기

> **CAUTION** — 토큰 값을 채팅·태스크 코멘트·저장소·셸 기록에 남기지 마십시오. 파일로만 옮깁니다.

**(가) 시계용 relay 토큰** — 서버에서 만듭니다. 무작위 32바이트(43자)입니다.

```bash
umask 077
python3 -c 'import secrets;print(secrets.token_urlsafe(32))' > /var/lib/clock-usage-relay/clock_token
chown clockrelay:clockrelay /var/lib/clock-usage-relay/clock_token
```

**(나) Anthropic OAuth 자격증명** — 구독 계정 소유자가 직접 합니다.

1. Linux 또는 WSL PC 에서 **새 빈 설정 폴더**로 Claude Code 를 실행합니다.
   ```bash
   export CLAUDE_CONFIG_DIR="$(mktemp -d)"
   claude            # /login → "Claude account with subscription" → 브라우저 로그인
   ```
   macOS 는 자격증명이 키체인에 저장돼 파일이 생기지 않으므로 Linux/WSL 을 씁니다(`SUSPECT` — macOS 미확인).
2. `/exit` 로 종료하고 `$CLAUDE_CONFIG_DIR/.credentials.json` 이 생겼는지 확인합니다.
3. 그 파일을 relay 서버로 옮깁니다(예: `scp`).
   ```bash
   install -m 600 -o clockrelay -g clockrelay .credentials.json /var/lib/clock-usage-relay/credentials.json
   ```
4. PC 쪽 임시 폴더를 지웁니다: `rm -rf "$CLAUDE_CONFIG_DIR"`. **이 로그인으로 Claude Code 를 다시 쓰지 않습니다.**

파일 형식:
```json
{"claudeAiOauth":{"accessToken":"…","refreshToken":"…","expiresAt":1794960000000,"scopes":["user:profile","user:inference"]}}
```

### 4.4 기동

```bash
systemctl daemon-reload
systemctl enable --now clock-usage-relay
systemctl status clock-usage-relay --no-pager
journalctl -u clock-usage-relay -n 50 --no-pager      # "listening on …" 확인. "only loopback" 경고가 없어야 합니다
```

### 4.5 relay 단독 확인 (relay 서버 안에서)

```bash
H=127.0.0.1        # RELAY_BIND 를 내부망 IP 로 정했으면 그 IP
TOKEN="$(cat /var/lib/clock-usage-relay/clock_token)"
curl -s http://$H:8787/healthz                                     # {"ok":true,"version":"1.3.0","samples":1,…}
curl -s -H "Authorization: Bearer $TOKEN" http://$H:8787/clock/usage; echo
curl -s -o /dev/null -w '%{http_code}\n' http://$H:8787/clock/usage   # 401
unset TOKEN
```

| 결과 | 판정 |
|---|---|
| `status:"ok"`, `Claude` 의 `pct` 가 숫자 | 정상. claude.ai 설정 → 사용량 화면의 주간 % 와 같은지 대조합니다 |
| `Fable` 이 `n/a` | 이 계정 응답에 Fable 행이 없습니다. 결과를 태스크에 남겨 주십시오 |
| `error:"usage_http_401"` / `refresh_failed` | 자격증명 무효. 4.3(나)를 다시 합니다 |
| `error:"usage_http_403"` | 토큰에 `user:profile` 권한이 없을 가능성이 큽니다 |
| `error:"network"` | relay 서버 → `api.anthropic.com`·`platform.claude.com` 443 경로 확인 |

---

## 5. 앞단 nginx 로 HTTPS 공개

시계를 회사 밖에서 쓰기 위한 구성입니다. **HTTPS 는 앞단 nginx 프록시 서버가 처리합니다.** relay 는 평문 HTTP 로 내부망에만 열립니다. 4장을 먼저 마친 상태를 전제로 합니다.

> **WARNING** — 이 장을 마치면 `GET /clock/usage` 하나가 인터넷에 공개됩니다. relay 토큰이 있어야 값을 줍니다. 토큰이 새면 누구나 사용률 숫자를 볼 수 있습니다(Anthropic 토큰은 새지 않습니다). 새었다고 의심되면 7.2절로 즉시 바꿉니다.

### 5.1 구성도

```
[시계] 회사 밖 Wi-Fi
   │ HTTPS 443  https://clock.example.com/clock/usage   (TLS 1.2, ISRG Root X1/X2 로 검증)
   ▼
[공유기] TCP 443(·80) 포워딩 → nginx 서버            ← 이미 nginx 로 쓰고 있는 포워딩을 그대로 씀
   ▼
[nginx 프록시 서버]  TLS 종료 · Let's Encrypt 인증서(fullchain) · GET /clock/usage 만 전달 · IP 당 분당 6회 제한
   │ HTTP  http://<relay 내부망 IP>:8787   X-Forwarded-For = 시계의 실제 접속 주소
   ▼
[relay LXC]  8787 은 nginx 서버 IP 에서만 열림 · 토큰 확인 · 10회 틀리면 그 주소 15분 차단
   │ HTTPS
   ▼
api.anthropic.com / platform.claude.com
```

| 구간 | 포트 | 비고 |
|---|---|---|
| 인터넷 → nginx | TCP 443 | 기존 nginx 의 443 을 그대로 씁니다. 새 포워딩은 필요 없습니다 |
| 인터넷 → nginx | TCP 80 | 인증서 발급·갱신(HTTP-01)을 쓰는 경우만. 기존 설정을 따릅니다 |
| nginx 서버 → relay | TCP 8787 | 내부망. relay 서버 방화벽에서 **nginx 서버 IP 만** 허용 |

### 5.2 준비물

1. **도메인 하나.** 예: `clock.example.com`. 아래의 `clock.example.com` 은 모두 실제 도메인으로 바꿉니다. 기존 nginx 가 쓰는 도메인의 하위 이름이면 됩니다.
2. **DNS A 레코드**: `clock.example.com` → nginx 서버가 받는 공인 IP. 기존 서비스 도메인과 같은 IP 입니다.
   ```bash
   getent ahostsv4 clock.example.com | head -1
   ```
3. **relay 서버 내부망 IP**(`RELAY_HOST`)와 **nginx 서버가 relay 로 접속할 때 쓰는 내부망 IP**(`NGINX_PROXY_IP`)를 확인합니다. 각 서버에서 `hostname -I`.
4. **nginx 서버에서 relay 로 접속 가능한지** 확인합니다(5.3절을 마친 뒤).
   ```bash
   curl -s http://RELAY_HOST:8787/healthz       # nginx 서버에서. {"ok":true}
   ```

### 5.3 relay 를 nginx 서버에만 열기 (relay 서버에서)

```bash
E=/etc/clock-usage-relay/relay.env
sed -i 's/^RELAY_BIND=.*/RELAY_BIND=0.0.0.0/' $E                        # 또는 relay 서버의 내부망 IP
sed -i 's/^RELAY_TRUSTED_PROXIES=.*/RELAY_TRUSTED_PROXIES=NGINX_PROXY_IP/' $E   # NGINX_PROXY_IP 를 실제 값으로
grep -q '^RELAY_TRUSTED_PROXIES=' $E || echo 'RELAY_TRUSTED_PROXIES=NGINX_PROXY_IP' >> $E
systemctl restart clock-usage-relay
journalctl -u clock-usage-relay -n 5 --no-pager      # "only loopback" 경고가 없어야 합니다
ss -ltnp | grep 8787
```

방화벽 — 8787 을 nginx 서버 IP 에서만 받습니다. 둘 중 하나를 씁니다.

- LXC 안 `ufw`:
  ```bash
  ufw allow from NGINX_PROXY_IP to any port 8787 proto tcp
  ufw deny 8787/tcp
  ufw status numbered
  ```
- Proxmox 방화벽: 해당 LXC 의 Firewall → Add → Direction `in`, Action `ACCEPT`, Source `NGINX_PROXY_IP`, Dest. port `8787`, Protocol `tcp`. 그 아래에 같은 포트 `DROP` 규칙을 둡니다.

> **CAUTION** — nginx → relay 구간은 평문 HTTP 입니다. 이 구간에는 시계의 relay 토큰이 그대로 흐릅니다. nginx 서버와 relay 서버가 같은 신뢰 내부망에 있을 때만 이 구성을 씁니다. 공유기 포트 포워딩에 8787 을 추가하지 않습니다.

### 5.4 nginx 설정 넣기 (nginx 서버에서)

동봉 파일 `deploy/nginx/clock-usage-relay.conf` 를 씁니다.

```nginx
limit_req_zone $binary_remote_addr zone=clock_usage:1m rate=6r/m;

upstream clock_usage_relay {
    server RELAY_HOST:8787;
    keepalive 2;
}

server {
    listen 443 ssl;
    listen [::]:443 ssl;
    server_name clock.example.com;

    ssl_certificate     /etc/letsencrypt/live/clock.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/clock.example.com/privkey.pem;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:...;   # 파일 전문 참조

    location = /clock/usage {
        limit_except GET { deny all; }
        limit_req zone=clock_usage burst=5 nodelay;
        limit_req_status 429;
        proxy_pass http://clock_usage_relay;
        proxy_set_header X-Forwarded-For $remote_addr;
        ...
    }
    location / { return 404; }
}
```

| 줄 | 뜻 |
|---|---|
| `limit_req_zone` · `limit_req` | 한 접속 주소당 분당 6회(순간 5회 여유)까지만 relay 로 넘깁니다. 시계는 10분마다(실패 시 2분마다) 조회하므로 정상 사용에는 걸리지 않습니다. 넘으면 nginx 가 `429` |
| `upstream … server RELAY_HOST:8787` | relay 서버 내부망 주소 |
| `ssl_certificate … fullchain.pem` | **반드시 `fullchain.pem`.** `cert.pem` 만 쓰면 중간 인증서가 빠져 시계의 CA 검증이 실패합니다 |
| `ssl_protocols TLSv1.2 TLSv1.3` | 시계(ESP8266 BearSSL)는 **TLS 1.2 만** 지원합니다. TLS 1.2 를 끄면 시계가 접속하지 못합니다 |
| `ssl_ciphers` | 시계가 지원하는 ECDHE + AES-GCM / CHACHA20 조합 |
| `location = /clock/usage` | 이 경로만 relay 로 넘깁니다. `/healthz` 를 포함한 나머지는 `404` |
| `limit_except GET { deny all; }` | GET 외 메서드는 `403` |
| `proxy_set_header X-Forwarded-For $remote_addr` | 시계가 보낸 `X-Forwarded-For` 를 버리고 실제 접속 주소로 **덮어씁니다.** 위조로 relay 차단을 피할 수 없습니다 |
| `proxy_read_timeout 40s` | relay 가 상위 호출(최대 15초)과 토큰 갱신을 기다리는 시간보다 길게 |

설치:

```bash
cp deploy/nginx/clock-usage-relay.conf /etc/nginx/conf.d/clock-usage-relay.conf
sed -i 's/clock\.example\.com/<실제 도메인>/g; s/RELAY_HOST/<relay 내부망 IP>/' /etc/nginx/conf.d/clock-usage-relay.conf
nginx -t && systemctl reload nginx
```

- 파일은 `http { }` 안에서 읽히는 곳에 둡니다. Debian 기본 `nginx.conf` 는 `conf.d/*.conf` 와 `sites-enabled/*` 를 모두 `http` 안에서 읽습니다. `limit_req_zone` 이 `http` 밖에 있으면 `nginx -t` 가 `"limit_req_zone" directive is not allowed here` 로 실패합니다.
- 기존 설정에 같은 `server_name` 이 있으면 `nginx -t` 가 `conflicting server name` 경고를 냅니다. 한쪽을 지웁니다.
- 기존 nginx 의 **기본(default) 443 서버**가 `ssl_protocols TLSv1.3` 만 허용하면 이 파일의 설정과 관계없이 TLS 1.2 가 막힐 수 있습니다(nginx 는 SNI 이전 단계에서 기본 서버 값을 씁니다). 5.6절의 TLS 1.2 확인으로 판정합니다.
- nginx 앞에 Cloudflare 프록시나 다른 L4/L7 프록시가 또 있으면 `$remote_addr` 이 그 프록시 주소가 됩니다. 그때는 nginx `real_ip_header`·`set_real_ip_from` 으로 실제 주소를 먼저 복원해야 합니다.
- 기본 접근 로그 형식에는 `Authorization` 헤더가 남지 않습니다. 로그 형식에 `$http_authorization` 을 넣지 않습니다.
- HTTP(80)는 이 파일에 넣지 않았습니다. 기존 80 → 443 리다이렉트가 있어도 됩니다. 시계는 리다이렉트를 따라가지 않으므로 시계 URL 은 처음부터 `https://` 로 씁니다.

### 5.5 인증서

시계의 기본 검증 방식(`CA`)은 **Let's Encrypt (ISRG Root X1·X2) 계열 인증서만** 통과시킵니다.

| nginx 인증서 | 시계 설정 |
|---|---|
| Let's Encrypt (certbot 등) | `CA` (기본). 갱신돼도 그대로 동작 |
| Let's Encrypt 와일드카드(`*.example.com`) | `CA`. 위와 같습니다 |
| 그 밖의 인증기관·사내 CA·자체 서명 | `fingerprint`. 갱신 때마다 시계의 지문을 바꿔야 합니다 |

Let's Encrypt 인증서가 아직 없으면 nginx 서버에서 발급합니다(certbot 예).

```bash
apt install -y certbot python3-certbot-nginx
certbot certonly --nginx -d clock.example.com        # /etc/letsencrypt/live/clock.example.com/ 에 생성
systemctl list-timers | grep certbot                 # 자동 갱신 타이머 확인
```

- 발급 전에는 `ssl_certificate` 경로가 없어 `nginx -t` 가 실패합니다. 발급 → 5.4절 설치 순서로 합니다.
- certbot 은 갱신 후 nginx 를 다시 읽습니다(`--nginx` 플러그인). 다른 방식으로 발급했다면 갱신 후 `systemctl reload nginx` 가 실행되도록 deploy hook 을 둡니다.

### 5.6 회사 밖에서 확인

회사 망이 아닌 곳(휴대폰 테더링 등)에서 합니다. 토큰이 셸 기록에 남지 않게 `read -s` 로 입력합니다.

```bash
D=clock.example.com
curl -s -o /dev/null -w '%{http_code}\n' https://$D/clock/usage           # 401 (토큰 없음, relay 응답)
read -rs TOKEN; echo                                                      # relay 토큰 붙여넣기 → Enter
curl -s -H "Authorization: Bearer $TOKEN" https://$D/clock/usage; echo    # 200, {"updated":…,"status":"ok"}
curl -s -o /dev/null -w '%{http_code}\n' https://$D/healthz              # 404 (공개 안 함)
curl -s -o /dev/null -w '%{http_code}\n' -X POST https://$D/clock/usage   # 403
unset TOKEN
```

TLS 1.2 와 인증서 체인 확인:
```bash
openssl s_client -connect $D:443 -servername $D -tls1_2 -cipher ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256 </dev/null 2>/dev/null | grep -E 'Protocol|Cipher is'
openssl s_client -connect $D:443 -servername $D -showcerts </dev/null 2>/dev/null | grep -E '^ *[0-9] s:|^ *i:|Verify return code'
```

| 확인 | 정상 |
|---|---|
| 첫 명령 | `Protocol : TLSv1.2` 와 ECDHE 암호 이름이 나옵니다. 연결 실패면 5.4절 기본 서버 `ssl_protocols` 를 봅니다 |
| `Verify return code` | `0 (ok)` |
| 체인 | 발급자(`i:`)에 `CN=ISRG Root X1` 또는 `CN=ISRG Root X2` 가 **하나 이상** 있어야 합니다. 체인이 `0 s:` 한 줄뿐이면 `fullchain.pem` 이 아닙니다 |

relay 쪽에서 실제 접속 주소가 보이는지 확인합니다(relay 서버):
```bash
journalctl -u clock-usage-relay --since '-5 min' --no-pager | tail -5    # 회사 밖 공인 IP 가 찍혀야 합니다. nginx 서버 IP 가 찍히면 5.3절 RELAY_TRUSTED_PROXIES 를 다시 봅니다
```

### 5.7 되돌리기

1. 시계 설정 페이지에서 JSON URL 을 비우고 저장합니다.
2. nginx 서버: `rm /etc/nginx/conf.d/clock-usage-relay.conf && nginx -t && systemctl reload nginx`. **이것만으로 외부 공개가 끝납니다.** 다른 서비스의 포워딩은 그대로 둡니다.
3. (선택) 인증서 삭제: `certbot delete --cert-name clock.example.com`.
4. (선택) DNS A 레코드 삭제.
5. relay 까지 지우려면 9장을 따릅니다.

---

## 6. 시계 설정 순서

시계 펌웨어 **0.1.3** 이 필요합니다(`hardware/README.ko.md` 9.3절).

1. 시계와 같은 Wi-Fi 에서 `http://<시계 IP>/` 에 접속해 `admin` 으로 로그인합니다.
2. **JSON URL**: `https://clock.example.com/clock/usage`
3. **Bearer token**: `/var/lib/clock-usage-relay/clock_token` 의 값
4. **HTTPS certificate check**: `CA (Let's Encrypt ISRG Root X1/X2) - default` (5.5절 표에 따라 `fingerprint`)
5. **TLS SHA-1 fingerprint**: `CA` 면 비웁니다. `fingerprint` 면 아래 명령 값(콜론 포함 그대로)
   ```bash
   openssl s_client -connect clock.example.com:443 -servername clock.example.com </dev/null 2>/dev/null | openssl x509 -noout -fingerprint -sha1
   ```
6. **Save** 를 누릅니다.
7. 설정 페이지 맨 위 `Usage feed` 가 `OK, 0 min ago`, 시계 화면 머리글이 `HH:MM 기준` 이면 정상입니다. 부팅 직후 잠깐 `시각 동기화 대기` 는 정상입니다.

| 설정 페이지 문구 | 원인 |
|---|---|
| `HTTP 401` | 토큰 불일치 |
| `HTTP 429` | relay 차단(7장) 또는 nginx 요청 수 제한 |
| `HTTP 404` | URL 경로 오타 (`/clock/usage` 정확히) |
| `HTTP 301` / `302` | URL 이 `http://`. `https://` 로 바꿉니다 |
| `HTTP 502` / `504` | nginx 가 relay 에 닿지 못함. 5.2절 4번, 5.3절 방화벽 |
| `connection failed (TLS …)` | 인증서 검증 실패(5.5·5.6절) 또는 시계 메모리 부족(`hardware/README.ko.md` 8.1절) |
| `connection failed` | 도메인·포워딩·회선 문제. 5.6절 `curl` 부터 |

---

## 7. 무차별 대입 방어와 토큰 관리

### 7.1 세 겹의 방어

1. **토큰 길이** — 4.3(가)는 무작위 43자를 만듭니다. 손으로 정한 짧은 값을 쓰지 않습니다.
   ```bash
   tr -d '\n' < /var/lib/clock-usage-relay/clock_token | wc -c     # 43
   ```
2. **nginx 요청 수 제한** — 접속 주소당 분당 6회(순간 5회 여유). 넘으면 nginx 가 `429` 를 줍니다.
3. **relay 차단** — 한 접속 주소가 10분 안에 토큰을 10번 틀리면 15분 동안 `429 {"error":"too_many_failures"}`. 차단 중에는 맞는 토큰도 거부합니다.
   ```bash
   journalctl -u clock-usage-relay --since today | grep 'locked out'
   ```

> **NOTE** — 시계 자신이 틀린 토큰으로 계속 조회하면(실패 시 2분 간격) 약 20분 뒤 그 시계도 15분간 막힙니다. 토큰을 다시 넣고 15분 기다리거나 relay 서버에서 `systemctl restart clock-usage-relay` 로 차단을 풉니다.

### 7.2 토큰 교체

```bash
umask 077; python3 -c 'import secrets;print(secrets.token_urlsafe(32))' > /var/lib/clock-usage-relay/clock_token
chown clockrelay:clockrelay /var/lib/clock-usage-relay/clock_token; systemctl restart clock-usage-relay
```
그다음 시계 설정 페이지의 Bearer token 을 새 값으로 바꿉니다.

---

## 8. 업그레이드 (1.2.0 → 1.3.0)

> **WARNING** — 운영 서버에서 root 로 실행하는 명령입니다. 보드 또는 보드가 지시한 담당자가 직접 실행합니다. 에이전트는 `ssh-rw` 로 실행하지 않습니다(회사 규칙 SOO-184).

systemd unit 파일과 `relay.env` 는 **바꿀 필요가 없습니다.** 표본 파일은 unit 의 `StateDirectory=clock-usage-relay`(= `/var/lib/clock-usage-relay`, 700) 안에 생기고, `ProtectSystem=strict` 에서도 이 폴더는 쓰기가 허용됩니다. 기존 `RELAY_CACHE_SECONDS=300` 은 표본 간격으로 그대로 쓰입니다.

1. relay 서버의 `python3` 에 `sqlite3` 모듈이 있는지 확인합니다. 버전 숫자가 나오면 다음 단계로 갑니다. `ModuleNotFoundError` 가 나오면 **여기서 멈추고** 결과를 태스크에 남깁니다.
   ```bash
   python3 -c 'import sqlite3; print(sqlite3.sqlite_version)'
   ```
2. Release `server-v1.3.0` 에서 `clock-usage-relay-1.3.0.tar.gz`·`SHA256SUMS` 를 받아 확인하고 풉니다.
   ```bash
   sha256sum -c SHA256SUMS
   tar xzf clock-usage-relay-1.3.0.tar.gz && cd clock-usage-relay-1.3.0
   ```
3. 지금 파일을 남기고 새 파일로 바꾼 뒤 재시작합니다.
   ```bash
   cp -p /opt/clock-usage-relay/clock_usage_relay.py /opt/clock-usage-relay/clock_usage_relay.py.1.2.0
   install -m 644 clock_usage_relay.py /opt/clock-usage-relay/
   systemctl restart clock-usage-relay
   journalctl -u clock-usage-relay -n 5 --no-pager    # "sample every 300s into /var/lib/clock-usage-relay/samples.db"
   ```
4. 표본이 쌓이는지 확인합니다. `samples` 가 1 이상이고 `latest_sample` 이 방금 시각이면 정상입니다. 5분 뒤 다시 보면 1 늘어 있어야 합니다.
   ```bash
   curl -s http://127.0.0.1:8787/healthz; echo          # RELAY_BIND 가 내부망 IP 면 그 IP
   ls -l /var/lib/clock-usage-relay/samples.db         # -rw------- clockrelay clockrelay
   ```
5. 시계 응답을 확인합니다(4.5절 방식). `"version":"1.3.0"` 이 보이면 정상입니다. 업그레이드 직후 1시간 동안 `delta_1h` 는 `null`, 24시간 동안 `delta_24h` 는 `null` 입니다(비교할 표본이 아직 없음).

**되돌리기.** 남겨 둔 1.2.0 파일로 바꾸고 재시작합니다. 1.2.0 은 `samples.db` 와 `RELAY_SAMPLE_*` 설정을 무시하므로 그대로 둬도 됩니다. 지우려면 `rm /var/lib/clock-usage-relay/samples.db` 입니다.

```bash
install -m 644 /opt/clock-usage-relay/clock_usage_relay.py.1.2.0 /opt/clock-usage-relay/clock_usage_relay.py
systemctl restart clock-usage-relay
```

### 8.1 이전 업그레이드 (1.1.0 → 1.2.0)


relay 서버에서 `server/` 폴더로 이동해 실행합니다.

```bash
install -m 644 clock_usage_relay.py /opt/clock-usage-relay/
systemctl restart clock-usage-relay
journalctl -u clock-usage-relay -n 5 --no-pager
```

- 1.2.0 은 설정 파일 형식이 같습니다. 5.3절의 `RELAY_BIND`·`RELAY_TRUSTED_PROXIES` 만 nginx 구성에 맞게 바꿉니다.
- `RELAY_TRUSTED_PROXIES` 에 IP·CIDR 이 아닌 값이 있으면 1.2.0 은 기동하지 않습니다(`invalid address or CIDR`). 1.1.0 은 이런 값을 조용히 무시했습니다.
- 1.1.0 절차대로 Caddy 를 이미 설치했다면 nginx 로 옮긴 뒤 지웁니다.
  ```bash
  systemctl disable --now caddy
  apt purge -y caddy
  rm -f /etc/apt/sources.list.d/caddy-stable.list /usr/share/keyrings/caddy-stable-archive-keyring.gpg
  rm -rf /etc/caddy /var/lib/caddy
  ```
  공유기에서 Caddy LXC 로 가던 80·443 포워딩도 지웁니다.

---

## 9. 제거

```bash
systemctl disable --now clock-usage-relay
rm -f /etc/systemd/system/clock-usage-relay.service && systemctl daemon-reload
rm -rf /opt/clock-usage-relay /etc/clock-usage-relay /var/lib/clock-usage-relay   # 자격증명 파일·표본 파일(samples.db) 포함 삭제
userdel clockrelay
```

relay 를 지워도 그 로그인은 Anthropic 쪽에 남아 있습니다. 완전히 끊으려면 claude.ai 설정에서 해당 세션을 해제합니다. nginx 쪽은 5.7절입니다.

---

## 10. 테스트

### 10.1 단위 시험

```bash
cd server
python3 -m unittest discover -s tests -v
```

목(mock) 상위 서버를 띄워 60건을 검사합니다. 계약 형식·값, OAuth 토큰이 응답에 섞이지 않음, 시계 인증 거부, 상위 호출 간격, 401 → 갱신 → 재시도와 회전된 토큰 저장(권한 600), 만료 임박 선갱신, 갱신 실패 시 `stale`, 첫 호출 실패 시 `error`, Fable 경로, 토큰 오입력 차단·만료, 신뢰 프록시의 `X-Forwarded-For` 처리, 1.2.0 추가분(CIDR·IPv4-mapped 신뢰 프록시, 잘못된 값 거부, 설정 누락 경고, IP 가 아닌 `X-Forwarded-For` 무시)이 포함됩니다.

1.3.0 추가 28건(임시 sqlite 파일 + 시각 주입): 표본 저장(소수 보존)·보존 기간 삭제, 실패한 호출은 표본 없음, 형식을 못 읽은 응답은 `raw_ok=0`, 재시작 뒤 표본 유지, 시계 요청이 상위를 호출하지 않음(호출 횟수 계측), 표본 나이로 정하는 `status`, `pct` 정수·`pct_raw` 소수, `hourly` 는 `?hourly=1` 에서만, `hourly` 포함 응답 4,096바이트 미만, `delta_1h`·`delta_24h` 기본·`null`·너무 오래된 비교 표본·초기화 보정·초기화 직전 표본 없음·음수 유지, 정시 구간 계산과 빈 구간, 표본 스레드 예외가 밖으로 나가지 않음, 표본 파일 쓰기 실패 보고, `/healthz` 표본 수, unit 파일의 `StateDirectory` 와 기본 경로 일치, 상위 폴더가 읽기 전용이어도 상태 폴더 안에만 쓰기(권한 600/700), 설정값 기본·하한·`RELAY_CACHE_SECONDS` 호환.

기존 1.2.0 시험 32건은 그대로 두되, 시계 요청이 더 이상 상위를 부르지 않으므로 상위 호출이 필요한 시험은 표본 1회(`sampler.tick()`) 뒤에 요청하도록 바꿨습니다. 캐시 시험 2건은 같은 내용을 표본 간격 기준으로 검사합니다.

### 10.2 nginx 연동 시험

```bash
cd server
tests/integration/run_nginx_it.sh /usr/sbin/nginx       # nginx 실행 파일 경로. root 불필요
```

동봉 `deploy/nginx/clock-usage-relay.conf` 를 그대로 쓰고, 주소·인증서만 시험용으로 바꿉니다. nginx 는 `127.0.0.3` 에서 relay(`127.0.0.2:18787`)로 접속해 **별도 서버**를 흉내 냅니다. 2026-11-18 결과(nginx 1.26.3-3+deb13u7, 12건 통과):

| 검사 | 결과 |
|---|---|
| TLS 1.2 + `ECDHE-ECDSA-AES128-GCM-SHA256` 핸드셰이크 후 토큰 있으면 200 | 통과 |
| 토큰 없음 401, `/healthz`·`/` 404, POST 403 | 통과 |
| 6회 초과 연속 요청 시 nginx 429 | 통과 |
| `X-Forwarded-For` 를 위조해도 실제 접속 주소 기준으로 10회 실패 후 relay 429 | 통과 |
| relay 로그의 접속 주소가 nginx 주소가 아닌 실제 접속 주소 | 통과 |
| nginx 접근 로그·relay 로그에 토큰 0건 | 통과 |
| `?hourly=1` 이 nginx 를 지나 relay 에 전달됨(항목마다 24개) | 통과 |
| 상위 호출 1회(표본 스레드, 시계 요청 수와 무관) | 통과 |
