**English** | [한국어](README.ko.md)

# clock-usage-relay (server) — 1.2.0

A relay server that fetches the **Claude subscription weekly-limit usage (%)** from Anthropic and serves it as a small JSON document for the smart clock to show at the bottom of its screen. It uses only the Python 3.9+ standard library (zero external packages).

The clock firmware is in [`../hardware/README.md`](../hardware/README.md).

```
[Clock ESP8266] --HTTPS, Bearer relay token--> [Front nginx proxy server] --HTTP (internal network)--> [relay :8787] --once per 5 min--> api.anthropic.com
                                               TLS termination, path limit, rate limit                 token check, cache, OAuth refresh
```

The Anthropic OAuth token stays inside the relay server only. The clock receives only the usage numbers.

| Version | Changes | Task |
|---|---|---|
| 1.0.0 | Initial release | SOO-598 |
| 1.1.0 | Default bind `127.0.0.1`, per-client-address lockout on repeated wrong tokens (`429`), recognition of the real client address behind a reverse proxy, bundled Caddy config | SOO-600 |
| **1.2.0** | **Switched to an nginx front proxy on a separate server.** `RELAY_TRUSTED_PROXIES` accepts CIDR (`10.0.0.0/24`) and IPv4-mapped addresses, refuses to start on invalid values, warns when the proxy setting is missing, ignores `X-Forwarded-For` values that are not IPs. Bundled nginx config file, removed Caddy config | SOO-612 |

## 0. Verification status

| Item | Status |
|---|---|
| Unit tests (`tests/test_relay.py`) | `CONFIRMED` — 32 passed (2026-11-15) |
| nginx integration tests (`tests/integration/run_nginx_it.sh`) | `CONFIRMED` — 11 passed with nginx 1.26.3 (Debian 13 package) + the bundled config file (2026-11-15). Section 10.2 |
| Actual Anthropic response format, presence of the Fable row | `UNVERIFIED` — check on the first call after installation (Section 4.5) |
| Clock connecting via a real domain and Let's Encrypt certificate | `UNVERIFIED` |

## 1. Data source investigation

### 1.1 Method and limitations

- Investigated: the usage-query and token-refresh code (strings and schema definitions) inside the Claude Code **2.1.319** executable installed in the container. Checked 2026-11-13.
- The endpoints were **not** called with a real token, because the task conditions say the board account's token is not to be handed over. The response format below is therefore evidence of "how Claude Code reads it"; the actual response is checked on the first call after installation. → All items are `UNVERIFIED (no live call yet)`.
- This is not an official public API. Anthropic may change the format or block it without notice.

### 1.2 Usage query endpoint

| Item | Value | Evidence |
|---|---|---|
| URL | `GET https://api.anthropic.com/api/oauth/usage` | Path string in client code (`/api/oauth/usage`, default base `https://api.anthropic.com`) |
| Auth | `Authorization: Bearer <OAuth access token>` | Same code |
| Header | `anthropic-beta: oauth-2025-04-20` | Beta constant common to OAuth requests |
| Required scope | Token that includes `user:profile` | Client description text "null when … a token without the profile scope" |
| Optional query | `?skip_spend=1`, etc. | The relay calls without a query |

### 1.3 Response fields (based on the schema Claude Code parses)

| Field | Format | Meaning | Used by relay |
|---|---|---|---|
| `five_hour` | `{utilization: number\|null, resets_at: ISO8601\|null}` | 5-hour session limit | Not used |
| `seven_day` | Same as above | **Overall weekly limit** | **Claude slot** |
| `seven_day_opus` | Same as above | Weekly Opus limit | Not used |
| `seven_day_sonnet` | Same as above | Weekly Sonnet limit | Not used |
| `seven_day_oauth_apps` | Same as above | Weekly OAuth app limit | Not used |
| `cinder_cove`, `wattle_ember` | Same as above (+ amount fields) | Internal code names, meaning unknown | Not used |
| `extra_usage` | `{is_enabled, monthly_limit, used_credits, utilization, currency}` | Extra usage (overage billing) | Not used |
| `limits[]` | `{kind, group, percent, resets_at, severity, is_active, scope:{model:{display_name}}}` | Server-defined list of display rows | **Fable slot**, fallback for Claude slot |

- `utilization` unit: **0–100 (%)** according to the client description text. The response headers (`anthropic-ratelimit-unified-*`) use 0–1, but the relay does not use the headers.
- `limits[].kind` examples: `session`, `weekly_all`, `weekly_scoped`. Per-model weekly rows have `kind="weekly_scoped"` with the model display name in `scope.model.display_name`. The example in the client description text is exactly `'Fable'`.

### 1.4 Presence of a Fable bucket

| Path | Status | Evidence |
|---|---|---|
| `limits[]` with `kind="weekly_scoped"` + `scope.model.display_name="Fable"` | **Present** (`SUSPECT` — may or may not exist depending on the account) | Description text "Per-model weekly windows from the server limits[] array" · example `'Fable'` |
| Bucket key `seven_day_overage_included` | Present (mainly in the response **headers**) | Client label table `seven_day_overage_included: "Fable limit"`, reset name `"Fable reset"` |
| A key named like `seven_day_fable` | **Absent** | 0 occurrences of that string in the entire executable |

Order in which the relay determines the Fable slot:
1. `percent` of the `limits[]` row that is `weekly_scoped` and whose model name contains `fable`.
2. Otherwise, top-level `seven_day_overage_included.utilization`.
3. If neither exists, `{"pct":null,"value":"n/a"}`. It is never filled with some other value.

### 1.5 Token expiry and refresh

| Item | Value | Evidence |
|---|---|---|
| Refresh URL | `POST https://platform.claude.com/v1/oauth/token` | Client `TOKEN_URL` |
| Body | JSON `{"grant_type":"refresh_token","refresh_token":…,"client_id":"9d1c250a-e61b-44d9-88ed-5944d1962f5e","scope":"user:profile user:inference …"}` | Same code |
| Response | `{access_token, refresh_token, expires_in, scope}` | Same code |
| Refresh token rotation | **Each response may carry a new refresh token.** If absent, the existing value is kept | Client handles it as `refresh_token = response value ?? existing value` |
| Storage format | `claudeAiOauth.{accessToken, refreshToken, expiresAt(ms)}` in `~/.claude/.credentials.json` | Claude Code storage format |

Relay behavior:
- Refreshes ahead of time when within 5 minutes of expiry. If the usage query returns 401/403, refreshes once and retries.
- **Atomically writes** the refresh result (new access/refresh token) back to the credentials file (temp file → `rename`, mode 600).
- If the refresh fails, serves the last value unchanged with `status:"stale"` and `error:"refresh_failed"`.

> **CAUTION** — Because the refresh token rotates, **if the relay and another Claude Code instance share the same login, one of them gets logged out.** Use credentials from a fresh login dedicated to the relay (Section 4.3).

> **NOTE** — The long-lived token created by `claude setup-token` has only the `user:inference` scope. It lacks `user:profile`, which the usage query needs, so it is **expected not to work** (`SUSPECT`).

---

## 2. JSON contract (final)

`GET /clock/usage` · header `Authorization: Bearer <relay token>`

```json
{"updated":"2026-11-13T19:30:16+09:00","period":"7d",
 "items":[{"label":"Claude","value":"42%","pct":42,"resets":"11-18 09:00"},
          {"label":"Fable","value":"17%","pct":17,"resets":"11-18 09:00"}],
 "status":"ok"}
```

| Field | Format | Description |
|---|---|---|
| `updated` | ISO8601 string \| `null` | Time of the last **successful** query (`RELAY_TZ`, default KST). `null` if it has never succeeded |
| `period` | `"7d"` | Fixed |
| `items[]` | Always 2, in the order `Claude`, `Fable` | |
| `items[].value` | String | `"42%"` or `"n/a"` |
| `items[].pct` | Integer 0–100 \| `null` | Rounded and range-clamped |
| `items[].resets` | `"MM-DD HH:MM"` (optional) | Weekly limit reset time. If unknown, the field is omitted entirely |
| `status` | `"ok"` \| `"stale"` \| `"error"` | **Additional field.** `stale` = the latest query failed, showing the previous value. `error` = no successful query yet |
| `error` | String (optional) | **Additional field.** `refresh_failed`, `usage_http_401`, `network`, `credentials_unreadable`, etc. |

- `status` and `error` are **additions** relative to the SOO-597 draft. The firmware can simply ignore unknown fields. Firmware 0.1.1 and later shows `status` in the header line (`hardware/README.md` Section 10).
- Other responses: missing or mismatched token `401 {"error":"unauthorized"}`, client address locked out after repeated wrong tokens `429 {"error":"too_many_failures"}` + `Retry-After` (seconds), other paths `404`, methods other than GET `405`, `GET /healthz` returns `{"ok":true}` without auth (not exposed externally in the Section 5 nginx setup).


---

## 3. Settings

Put these in `/etc/clock-usage-relay/relay.env` (example: `deploy/relay.env.example`).

| Variable | Default | Description |
|---|---|---|
| `RELAY_BIND` | `127.0.0.1` | Bind address. **If nginx is on a different server**, change it to the relay server's internal IP or `0.0.0.0` (Section 5.3) |
| `RELAY_PORT` | `8787` | Port |
| `RELAY_TRUSTED_PROXIES` | `127.0.0.1,::1` | nginx server address. Comma-separated, CIDR allowed (`10.0.0.12`, `10.0.0.0/24`). Only for requests from these addresses is the last value of `X-Forwarded-For` treated as the real client address. The relay refuses to start on invalid values |
| `RELAY_CACHE_SECONDS` | `300` | Upstream call interval (minimum 30). This interval is respected even after failures |
| `RELAY_TZ` | `Asia/Seoul` | Display time zone |
| `RELAY_CREDENTIALS_FILE` | (required) | File in Claude Code `.credentials.json` format. The relay rewrites it on refresh |
| `RELAY_CLOCK_TOKEN_FILE` | (required, or `RELAY_CLOCK_TOKEN`) | Clock auth token. At least 16 characters |
| `RELAY_AUTH_MAX_FAILURES` | `10` | Lock out a client address after this many wrong tokens within the window |
| `RELAY_AUTH_WINDOW_SECONDS` | `600` | Window for counting failures (seconds) |
| `RELAY_AUTH_LOCKOUT_SECONDS` | `900` | Lockout duration (seconds). During lockout even a correct token gets `429` |
| `RELAY_USAGE_URL` · `RELAY_TOKEN_URL` · `RELAY_OAUTH_CLIENT_ID` · `RELAY_OAUTH_SCOPES` · `RELAY_OAUTH_BETA` | Values from the Section 1 tables | For changing these without code edits if the upstream format changes |

> **CAUTION** — If nginx is on a different server and its address is not in `RELAY_TRUSTED_PROXIES`, the relay sees every clock as the single nginx address. Then anyone can **lock out all clocks for 15 minutes** just by sending a wrong token 10 times. If the relay starts in this state, the log shows a `RELAY_TRUSTED_PROXIES has only loopback` warning.

---

## 4. Installing the relay

> **WARNING** — This procedure consists of commands run as root on a production server. The board, or a person the board designates, runs them directly. Agents do not run them via `ssh-rw` (company rule SOO-184).

### 4.1 Where to install

- Rather than placing it directly on the hypervisor (Proxmox) host, install it in a single Debian 13 LXC container (128–256 MB memory, 1 GB disk). This is the recommended setup.
- Requirements: `python3` 3.9 or later, systemd, outbound HTTPS (443) access.
- It can also live on the same server as the nginx proxy. In that case, leave `RELAY_BIND` and `RELAY_TRUSTED_PROXIES` at their defaults and set `RELAY_HOST` in the nginx config to `127.0.0.1`.

### 4.2 Installing the files

After obtaining the repo, run these from the `server/` folder.

```bash
python3 --version                                  # 3.9 or later
useradd --system --no-create-home --shell /usr/sbin/nologin clockrelay
install -d -m 755 /opt/clock-usage-relay
install -m 644 clock_usage_relay.py /opt/clock-usage-relay/
install -d -m 750 -o root -g clockrelay /etc/clock-usage-relay
install -m 640 -o root -g clockrelay deploy/relay.env.example /etc/clock-usage-relay/relay.env
install -d -m 700 -o clockrelay -g clockrelay /var/lib/clock-usage-relay
install -m 644 deploy/clock-usage-relay.service /etc/systemd/system/
```

Next, replace `NGINX_PROXY_IP` in `/etc/clock-usage-relay/relay.env` with the nginx server's internal IP (Section 5.3). If the relay is on the same server as nginx, change it to `RELAY_BIND=127.0.0.1` and delete the `RELAY_TRUSTED_PROXIES` line.

### 4.3 Adding the secrets

> **CAUTION** — Do not leave token values in chat, task comments, the repository, or shell history. Move them only via files.

**(a) Relay token for the clock** — Generate it on the server. It is 32 random bytes (43 characters).

```bash
umask 077
python3 -c 'import secrets;print(secrets.token_urlsafe(32))' > /var/lib/clock-usage-relay/clock_token
chown clockrelay:clockrelay /var/lib/clock-usage-relay/clock_token
```

**(b) Anthropic OAuth credentials** — The subscription account owner does this personally.

1. On a Linux or WSL PC, run Claude Code with a **new, empty config folder**.
   ```bash
   export CLAUDE_CONFIG_DIR="$(mktemp -d)"
   claude            # /login → "Claude account with subscription" → browser login
   ```
   On macOS the credentials are stored in the Keychain and no file is created, so use Linux/WSL (`SUSPECT` — macOS not verified).
2. Exit with `/exit` and confirm that `$CLAUDE_CONFIG_DIR/.credentials.json` was created.
3. Move that file to the relay server (e.g. with `scp`).
   ```bash
   install -m 600 -o clockrelay -g clockrelay .credentials.json /var/lib/clock-usage-relay/credentials.json
   ```
4. Delete the temporary folder on the PC: `rm -rf "$CLAUDE_CONFIG_DIR"`. **Do not use this login with Claude Code again.**

File format:
```json
{"claudeAiOauth":{"accessToken":"…","refreshToken":"…","expiresAt":1794960000000,"scopes":["user:profile","user:inference"]}}
```

### 4.4 Starting

```bash
systemctl daemon-reload
systemctl enable --now clock-usage-relay
systemctl status clock-usage-relay --no-pager
journalctl -u clock-usage-relay -n 50 --no-pager      # check for "listening on …". There must be no "only loopback" warning
```

### 4.5 Checking the relay alone (on the relay server)

```bash
H=127.0.0.1        # if RELAY_BIND is set to an internal IP, use that IP
TOKEN="$(cat /var/lib/clock-usage-relay/clock_token)"
curl -s http://$H:8787/healthz                                     # {"ok":true}
curl -s -H "Authorization: Bearer $TOKEN" http://$H:8787/clock/usage; echo
curl -s -o /dev/null -w '%{http_code}\n' http://$H:8787/clock/usage   # 401
unset TOKEN
```

| Result | Verdict |
|---|---|
| `status:"ok"`, `pct` of `Claude` is a number | Normal. Compare it with the weekly % on the claude.ai Settings → Usage screen |
| `Fable` is `n/a` | This account's response has no Fable row. Please record the result in the task |
| `error:"usage_http_401"` / `refresh_failed` | Invalid credentials. Redo 4.3 (b) |
| `error:"usage_http_403"` | The token most likely lacks the `user:profile` permission |
| `error:"network"` | Check the route from the relay server → `api.anthropic.com` and `platform.claude.com` on 443 |

---

## 5. Exposing over HTTPS via the front nginx

This setup lets the clock be used outside the office. **HTTPS is handled by the front nginx proxy server.** The relay is open only on the internal network over plain HTTP. This chapter assumes Section 4 is already complete.

> **WARNING** — After this chapter, the single endpoint `GET /clock/usage` is exposed to the internet. It returns values only with the relay token. If the token leaks, anyone can see the usage numbers (the Anthropic token does not leak). If you suspect a leak, rotate it immediately per Section 7.2.

### 5.1 Architecture

```
[Clock] Wi-Fi outside the office
   │ HTTPS 443  https://clock.example.com/clock/usage   (TLS 1.2, verified against ISRG Root X1/X2)
   ▼
[Router] TCP 443(·80) forwarding → nginx server      ← reuses the forwarding already used for nginx
   ▼
[nginx proxy server]  TLS termination · Let's Encrypt certificate (fullchain) · forwards GET /clock/usage only · 6 req/min per IP
   │ HTTP  http://<relay internal IP>:8787   X-Forwarded-For = clock's real client address
   ▼
[relay LXC]  8787 open only to the nginx server IP · token check · 10 wrong tokens lock that address out for 15 min
   │ HTTPS
   ▼
api.anthropic.com / platform.claude.com
```

| Segment | Port | Notes |
|---|---|---|
| Internet → nginx | TCP 443 | Reuse the existing nginx 443. No new forwarding needed |
| Internet → nginx | TCP 80 | Only if you use HTTP-01 for certificate issuance/renewal. Follow the existing setup |
| nginx server → relay | TCP 8787 | Internal network. On the relay server firewall, allow **only the nginx server IP** |

### 5.2 Prerequisites

1. **One domain.** E.g. `clock.example.com`. Replace every `clock.example.com` below with your real domain. A subdomain of a domain the existing nginx already serves is fine.
2. **DNS A record**: `clock.example.com` → the public IP that the nginx server receives on. This is the same IP as the existing service domains.
   ```bash
   getent ahostsv4 clock.example.com | head -1
   ```
3. Find the **relay server's internal IP** (`RELAY_HOST`) and the **internal IP the nginx server uses to connect to the relay** (`NGINX_PROXY_IP`). Run `hostname -I` on each server.
4. Confirm **the nginx server can reach the relay** (after completing Section 5.3).
   ```bash
   curl -s http://RELAY_HOST:8787/healthz       # on the nginx server. {"ok":true}
   ```

### 5.3 Opening the relay to the nginx server only (on the relay server)

```bash
E=/etc/clock-usage-relay/relay.env
sed -i 's/^RELAY_BIND=.*/RELAY_BIND=0.0.0.0/' $E                        # or the relay server's internal IP
sed -i 's/^RELAY_TRUSTED_PROXIES=.*/RELAY_TRUSTED_PROXIES=NGINX_PROXY_IP/' $E   # replace NGINX_PROXY_IP with the real value
grep -q '^RELAY_TRUSTED_PROXIES=' $E || echo 'RELAY_TRUSTED_PROXIES=NGINX_PROXY_IP' >> $E
systemctl restart clock-usage-relay
journalctl -u clock-usage-relay -n 5 --no-pager      # there must be no "only loopback" warning
ss -ltnp | grep 8787
```

Firewall — accept 8787 only from the nginx server IP. Use one of the following.

- `ufw` inside the LXC:
  ```bash
  ufw allow from NGINX_PROXY_IP to any port 8787 proto tcp
  ufw deny 8787/tcp
  ufw status numbered
  ```
- Proxmox firewall: on that LXC, Firewall → Add → Direction `in`, Action `ACCEPT`, Source `NGINX_PROXY_IP`, Dest. port `8787`, Protocol `tcp`. Below it, add a `DROP` rule for the same port.

> **CAUTION** — The nginx → relay segment is plain HTTP. The clock's relay token travels over it in the clear. Use this setup only when the nginx server and the relay server are on the same trusted internal network. Do not add 8787 to the router's port forwarding.

### 5.4 Adding the nginx config (on the nginx server)

Use the bundled file `deploy/nginx/clock-usage-relay.conf`.

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
    ssl_ciphers ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:...;   # see the full file

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

| Line | Meaning |
|---|---|
| `limit_req_zone` · `limit_req` | Passes at most 6 requests per minute per client address (burst allowance of 5) to the relay. The clock polls every 10 minutes (every 2 minutes after a failure), so normal use never hits this. Beyond it, nginx returns `429` |
| `upstream … server RELAY_HOST:8787` | The relay server's internal address |
| `ssl_certificate … fullchain.pem` | **Must be `fullchain.pem`.** With `cert.pem` alone the intermediate certificate is missing and the clock's CA verification fails |
| `ssl_protocols TLSv1.2 TLSv1.3` | The clock (ESP8266 BearSSL) supports **TLS 1.2 only**. If TLS 1.2 is disabled, the clock cannot connect |
| `ssl_ciphers` | ECDHE + AES-GCM / CHACHA20 combinations the clock supports |
| `location = /clock/usage` | Only this path is passed to the relay. Everything else, including `/healthz`, gets `404` |
| `limit_except GET { deny all; }` | Methods other than GET get `403` |
| `proxy_set_header X-Forwarded-For $remote_addr` | Discards any `X-Forwarded-For` sent by the client and **overwrites** it with the real client address. Spoofing cannot be used to evade the relay lockout |
| `proxy_read_timeout 40s` | Longer than the time the relay may wait for the upstream call (up to 15 s) plus a token refresh |

Installation:

```bash
cp deploy/nginx/clock-usage-relay.conf /etc/nginx/conf.d/clock-usage-relay.conf
sed -i 's/clock\.example\.com/<actual domain>/g; s/RELAY_HOST/<relay internal IP>/' /etc/nginx/conf.d/clock-usage-relay.conf
nginx -t && systemctl reload nginx
```

- Place the file where it is read inside `http { }`. The Debian default `nginx.conf` reads both `conf.d/*.conf` and `sites-enabled/*` inside `http`. If `limit_req_zone` is outside `http`, `nginx -t` fails with `"limit_req_zone" directive is not allowed here`.
- If the existing config already has the same `server_name`, `nginx -t` emits a `conflicting server name` warning. Remove one of them.
- If the existing nginx **default 443 server** allows only `ssl_protocols TLSv1.3`, TLS 1.2 may be blocked regardless of this file's settings (nginx uses the default server's values before SNI is processed). Use the TLS 1.2 check in Section 5.6 to determine this.
- If there is another Cloudflare proxy or other L4/L7 proxy in front of nginx, `$remote_addr` becomes that proxy's address. In that case, first restore the real address with nginx `real_ip_header` and `set_real_ip_from`.
- The default access log format does not record the `Authorization` header. Do not add `$http_authorization` to the log format.
- HTTP (80) is not included in this file. An existing 80 → 443 redirect is fine. The clock does not follow redirects, so write the clock URL with `https://` from the start.

### 5.5 Certificates

The clock's default verification mode (`CA`) accepts **only Let's Encrypt (ISRG Root X1·X2) chain certificates**.

| nginx certificate | Clock setting |
|---|---|
| Let's Encrypt (certbot, etc.) | `CA` (default). Keeps working after renewal |
| Let's Encrypt wildcard (`*.example.com`) | `CA`. Same as above |
| Any other CA, internal CA, or self-signed | `fingerprint`. The fingerprint on the clock must be updated on every renewal |

If you do not yet have a Let's Encrypt certificate, issue one on the nginx server (certbot example).

```bash
apt install -y certbot python3-certbot-nginx
certbot certonly --nginx -d clock.example.com        # created in /etc/letsencrypt/live/clock.example.com/
systemctl list-timers | grep certbot                 # check the auto-renewal timer
```

- Before issuance, the `ssl_certificate` path does not exist and `nginx -t` fails. Issue first, then install per Section 5.4.
- certbot reloads nginx after renewal (`--nginx` plugin). If you issued the certificate another way, set up a deploy hook so `systemctl reload nginx` runs after renewal.

### 5.6 Checking from outside the office

Do this from outside the company network (e.g. phone tethering). Enter the token with `read -s` so it does not stay in shell history.

```bash
D=clock.example.com
curl -s -o /dev/null -w '%{http_code}\n' https://$D/clock/usage           # 401 (no token, relay response)
read -rs TOKEN; echo                                                      # paste the relay token → Enter
curl -s -H "Authorization: Bearer $TOKEN" https://$D/clock/usage; echo    # 200, {"updated":…,"status":"ok"}
curl -s -o /dev/null -w '%{http_code}\n' https://$D/healthz              # 404 (not exposed)
curl -s -o /dev/null -w '%{http_code}\n' -X POST https://$D/clock/usage   # 403
unset TOKEN
```

Check TLS 1.2 and the certificate chain:
```bash
openssl s_client -connect $D:443 -servername $D -tls1_2 -cipher ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256 </dev/null 2>/dev/null | grep -E 'Protocol|Cipher is'
openssl s_client -connect $D:443 -servername $D -showcerts </dev/null 2>/dev/null | grep -E '^ *[0-9] s:|^ *i:|Verify return code'
```

| Check | Expected |
|---|---|
| First command | Shows `Protocol : TLSv1.2` and an ECDHE cipher name. If the connection fails, check the default server's `ssl_protocols` per Section 5.4 |
| `Verify return code` | `0 (ok)` |
| Chain | The issuer (`i:`) lines must include **at least one** `CN=ISRG Root X1` or `CN=ISRG Root X2`. If the chain is only the single `0 s:` line, it is not `fullchain.pem` |

Confirm that the relay sees the real client address (on the relay server):
```bash
journalctl -u clock-usage-relay --since '-5 min' --no-pager | tail -5    # should show the public IP outside the office. If it shows the nginx server IP, recheck RELAY_TRUSTED_PROXIES in Section 5.3
```

### 5.7 Rolling back

1. On the clock settings page, clear the JSON URL and save.
2. nginx server: `rm /etc/nginx/conf.d/clock-usage-relay.conf && nginx -t && systemctl reload nginx`. **This alone ends the external exposure.** Leave the forwarding for other services as is.
3. (Optional) Delete the certificate: `certbot delete --cert-name clock.example.com`.
4. (Optional) Delete the DNS A record.
5. To remove the relay as well, follow Section 9.

---

## 6. Clock setup steps

Requires clock firmware **0.1.3** (`hardware/README.md` Section 9.3).

1. On the same Wi-Fi as the clock, open `http://<clock IP>/` and log in as `admin`.
2. **JSON URL**: `https://clock.example.com/clock/usage`
3. **Bearer token**: the value of `/var/lib/clock-usage-relay/clock_token`
4. **HTTPS certificate check**: `CA (Let's Encrypt ISRG Root X1/X2) - default` (or `fingerprint`, per the table in Section 5.5)
5. **TLS SHA-1 fingerprint**: leave empty for `CA`. For `fingerprint`, use the value from the command below (as is, including colons)
   ```bash
   openssl s_client -connect clock.example.com:443 -servername clock.example.com </dev/null 2>/dev/null | openssl x509 -noout -fingerprint -sha1
   ```
6. Click **Save**.
7. It is working if `Usage feed` at the top of the settings page shows `OK, 0 min ago` and the clock screen header shows `HH:MM 기준` ("as of HH:MM"). Briefly showing `시각 동기화 대기` ("waiting for time sync") right after boot is normal.

| Settings page message | Cause |
|---|---|
| `HTTP 401` | Token mismatch |
| `HTTP 429` | Relay lockout (Section 7) or nginx rate limit |
| `HTTP 404` | Typo in the URL path (must be exactly `/clock/usage`) |
| `HTTP 301` / `302` | URL is `http://`. Change it to `https://` |
| `HTTP 502` / `504` | nginx cannot reach the relay. Section 5.2 item 4, Section 5.3 firewall |
| `connection failed (TLS …)` | Certificate verification failure (Sections 5.5 and 5.6) or the clock is out of memory (`hardware/README.md` Section 8.1) |
| `connection failed` | Domain, forwarding, or line problem. Start with the `curl` checks in Section 5.6 |

---

## 7. Brute-force protection and token management

### 7.1 Three layers of defense

1. **Token length** — 4.3 (a) generates 43 random characters. Do not use a short hand-picked value.
   ```bash
   tr -d '\n' < /var/lib/clock-usage-relay/clock_token | wc -c     # 43
   ```
2. **nginx rate limit** — 6 requests per minute per client address (burst allowance of 5). Beyond it, nginx returns `429`.
3. **Relay lockout** — If a client address sends a wrong token 10 times within 10 minutes, it gets `429 {"error":"too_many_failures"}` for 15 minutes. During lockout even a correct token is rejected.
   ```bash
   journalctl -u clock-usage-relay --since today | grep 'locked out'
   ```

> **NOTE** — If the clock itself keeps polling with a wrong token (every 2 minutes after failures), that clock also gets locked out for 15 minutes after about 20 minutes. Re-enter the token and wait 15 minutes, or clear the lockout with `systemctl restart clock-usage-relay` on the relay server.

### 7.2 Rotating the token

```bash
umask 077; python3 -c 'import secrets;print(secrets.token_urlsafe(32))' > /var/lib/clock-usage-relay/clock_token
chown clockrelay:clockrelay /var/lib/clock-usage-relay/clock_token; systemctl restart clock-usage-relay
```
Then change the Bearer token on the clock settings page to the new value.

---

## 8. Upgrading (1.1.0 → 1.2.0)

On the relay server, change to the `server/` folder and run:

```bash
install -m 644 clock_usage_relay.py /opt/clock-usage-relay/
systemctl restart clock-usage-relay
journalctl -u clock-usage-relay -n 5 --no-pager
```

- 1.2.0 uses the same settings file format. Change only `RELAY_BIND` and `RELAY_TRUSTED_PROXIES` from Section 5.3 to fit the nginx setup.
- If `RELAY_TRUSTED_PROXIES` contains a value that is not an IP or CIDR, 1.2.0 refuses to start (`invalid address or CIDR`). 1.1.0 silently ignored such values.
- If you already installed Caddy following the 1.1.0 procedure, remove it after moving to nginx.
  ```bash
  systemctl disable --now caddy
  apt purge -y caddy
  rm -f /etc/apt/sources.list.d/caddy-stable.list /usr/share/keyrings/caddy-stable-archive-keyring.gpg
  rm -rf /etc/caddy /var/lib/caddy
  ```
  Also remove the 80/443 forwarding on the router that pointed to the Caddy LXC.

---

## 9. Uninstalling

```bash
systemctl disable --now clock-usage-relay
rm -f /etc/systemd/system/clock-usage-relay.service && systemctl daemon-reload
rm -rf /opt/clock-usage-relay /etc/clock-usage-relay /var/lib/clock-usage-relay   # deletes the credentials file too
userdel clockrelay
```

Removing the relay leaves that login active on the Anthropic side. To cut it off completely, revoke the session in claude.ai settings. For the nginx side, see Section 5.7.

---

## 10. Tests

### 10.1 Unit tests

```bash
cd server
python3 -m unittest discover -s tests -v
```

Starts a mock upstream server and runs 32 checks. They cover: contract format and values, OAuth tokens never leaking into responses, clock auth rejection, caching, 401 → refresh → retry and saving the rotated token (mode 600), proactive refresh near expiry, `stale` on refresh failure, `error` on first-call failure, the Fable path, wrong-token lockout and its expiry, `X-Forwarded-For` handling for trusted proxies, and the 1.2.0 additions (CIDR and IPv4-mapped trusted proxies, rejection of invalid values, missing-setting warning, ignoring non-IP `X-Forwarded-For`).

### 10.2 nginx integration tests

```bash
cd server
tests/integration/run_nginx_it.sh /usr/sbin/nginx       # path to the nginx executable. root not required
```

Uses the bundled `deploy/nginx/clock-usage-relay.conf` as is, replacing only the addresses and certificate with test values. nginx connects from `127.0.0.3` to the relay (`127.0.0.2:18787`) to simulate a **separate server**. Results on 2026-11-15 (nginx 1.26.3, 11 passed):

| Check | Result |
|---|---|
| 200 with a token after a TLS 1.2 + `ECDHE-ECDSA-AES128-GCM-SHA256` handshake | Pass |
| No token 401, `/healthz` and `/` 404, POST 403 | Pass |
| nginx 429 on more than 6 consecutive requests | Pass |
| relay 429 after 10 failures keyed on the real client address, even with a spoofed `X-Forwarded-For` | Pass |
| Client address in the relay log is the real client address, not the nginx address | Pass |
| 0 tokens in the nginx access log and the relay log | Pass |
| 1 upstream call (cache) | Pass |
