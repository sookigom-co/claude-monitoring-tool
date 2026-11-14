#!/usr/bin/env python3
"""Smart clock usage relay: serves Claude subscription weekly utilization as a small JSON."""

import hmac
import ipaddress
import json
import logging
import os
import re
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    from zoneinfo import ZoneInfo
except ImportError:  # pragma: no cover
    ZoneInfo = None

VERSION = "1.2.0"

DEFAULTS = {
    "RELAY_BIND": "127.0.0.1",
    "RELAY_PORT": "8787",
    "RELAY_CACHE_SECONDS": "300",
    "RELAY_TZ": "Asia/Seoul",
    "RELAY_USAGE_URL": "https://api.anthropic.com/api/oauth/usage",
    "RELAY_TOKEN_URL": "https://platform.claude.com/v1/oauth/token",
    "RELAY_OAUTH_CLIENT_ID": "9d1c250a-e61b-44d9-88ed-5944d1962f5e",
    "RELAY_OAUTH_SCOPES": "user:profile user:inference user:sessions:claude_code user:mcp_servers user:file_upload",
    "RELAY_OAUTH_BETA": "oauth-2025-04-20",
    "RELAY_HTTP_TIMEOUT": "15",
    "RELAY_REFRESH_SKEW_SECONDS": "300",
    "RELAY_USER_AGENT": "clock-usage-relay/" + VERSION,
    "RELAY_TRUSTED_PROXIES": "127.0.0.1,::1",
    "RELAY_AUTH_MAX_FAILURES": "10",
    "RELAY_AUTH_WINDOW_SECONDS": "600",
    "RELAY_AUTH_LOCKOUT_SECONDS": "900",
}

log = logging.getLogger("clock-usage-relay")

_SECRET_PATTERN = re.compile(r"(sk-ant-[A-Za-z0-9_\-]{4})[A-Za-z0-9_\-]+|(Bearer\s+)\S+")


def mask(text):
    """Masks anything that looks like a token."""
    return _SECRET_PATTERN.sub(lambda m: (m.group(1) or m.group(2)) + "***", str(text))


class Config:
    def __init__(self, env=None):
        env = dict(os.environ if env is None else env)
        get = lambda k: env.get(k) or DEFAULTS.get(k)
        self.bind = get("RELAY_BIND")
        self.port = int(get("RELAY_PORT"))
        self.cache_seconds = max(30, int(get("RELAY_CACHE_SECONDS")))
        self.usage_url = get("RELAY_USAGE_URL")
        self.token_url = get("RELAY_TOKEN_URL")
        self.client_id = get("RELAY_OAUTH_CLIENT_ID")
        self.scopes = get("RELAY_OAUTH_SCOPES")
        self.beta = get("RELAY_OAUTH_BETA")
        self.http_timeout = float(get("RELAY_HTTP_TIMEOUT"))
        self.refresh_skew = int(get("RELAY_REFRESH_SKEW_SECONDS"))
        self.user_agent = get("RELAY_USER_AGENT")
        self.trusted_proxies = _parse_networks(get("RELAY_TRUSTED_PROXIES"))
        self.auth_max_failures = max(1, int(get("RELAY_AUTH_MAX_FAILURES")))
        self.auth_window = max(1, int(get("RELAY_AUTH_WINDOW_SECONDS")))
        self.auth_lockout = max(1, int(get("RELAY_AUTH_LOCKOUT_SECONDS")))
        self.credentials_file = env.get("RELAY_CREDENTIALS_FILE")
        self.tz = _load_tz(get("RELAY_TZ"))
        self.clock_token = _read_secret(env, "RELAY_CLOCK_TOKEN")
        if not self.credentials_file:
            raise SystemExit("RELAY_CREDENTIALS_FILE is required")
        if not self.clock_token or len(self.clock_token) < 16:
            raise SystemExit("RELAY_CLOCK_TOKEN or RELAY_CLOCK_TOKEN_FILE (>=16 chars) is required")


    def is_trusted_proxy(self, address):
        ip = _parse_ip(address)
        return ip is not None and any(ip in net for net in self.trusted_proxies)


def _parse_networks(text):
    networks = []
    for item in text.split(","):
        item = item.strip()
        if not item:
            continue
        try:
            networks.append(ipaddress.ip_network(item, strict=False))
        except ValueError:
            raise SystemExit("RELAY_TRUSTED_PROXIES: invalid address or CIDR %r" % item)
    return networks


def _parse_ip(text):
    """Parses an address, unwrapping IPv4-mapped IPv6 such as ::ffff:10.0.0.5."""
    try:
        ip = ipaddress.ip_address(str(text).strip())
    except ValueError:
        return None
    if ip.version == 6 and ip.ipv4_mapped:
        return ip.ipv4_mapped
    return ip


def _load_tz(name):
    if ZoneInfo is not None:
        try:
            return ZoneInfo(name)
        except Exception:
            log.warning("timezone %s unavailable, falling back to UTC+09:00", name)
    return timezone(timedelta(hours=9))


def _read_secret(env, key):
    path = env.get(key + "_FILE")
    if path:
        with open(path, encoding="utf-8") as f:
            return f.read().strip()
    return (env.get(key) or "").strip()


class CredentialStore:
    """Reads and atomically rewrites OAuth credentials in Claude Code's .credentials.json layout."""

    def __init__(self, path):
        self.path = path
        self._lock = threading.Lock()

    def load(self):
        with open(self.path, encoding="utf-8") as f:
            data = json.load(f)
        oauth = data.get("claudeAiOauth", data) if isinstance(data, dict) else {}
        return {
            "accessToken": oauth.get("accessToken") or oauth.get("access_token"),
            "refreshToken": oauth.get("refreshToken") or oauth.get("refresh_token"),
            "expiresAt": oauth.get("expiresAt") or oauth.get("expires_at"),
        }

    def save(self, access_token, refresh_token, expires_at_ms):
        with self._lock:
            try:
                with open(self.path, encoding="utf-8") as f:
                    data = json.load(f)
                if not isinstance(data, dict):
                    data = {}
            except (OSError, ValueError):
                data = {}
            oauth = data.setdefault("claudeAiOauth", {})
            oauth["accessToken"] = access_token
            oauth["refreshToken"] = refresh_token
            oauth["expiresAt"] = expires_at_ms
            directory = os.path.dirname(os.path.abspath(self.path))
            fd, tmp = tempfile.mkstemp(prefix=".credentials-", dir=directory)
            try:
                os.fchmod(fd, 0o600)
                with os.fdopen(fd, "w", encoding="utf-8") as f:
                    json.dump(data, f)
                os.replace(tmp, self.path)
            except Exception:
                if os.path.exists(tmp):
                    os.unlink(tmp)
                raise


class UpstreamError(Exception):
    def __init__(self, code, status=None):
        super().__init__(code)
        self.code = code
        self.status = status


class AnthropicClient:
    def __init__(self, cfg, store):
        self.cfg = cfg
        self.store = store

    def _request(self, url, method="GET", headers=None, body=None):
        req = urllib.request.Request(url, data=body, method=method, headers=headers or {})
        try:
            with urllib.request.urlopen(req, timeout=self.cfg.http_timeout) as resp:
                raw = resp.read(1024 * 1024)
                return resp.status, raw
        except urllib.error.HTTPError as e:
            return e.code, b""
        except (urllib.error.URLError, OSError, ValueError) as e:
            raise UpstreamError("network", None) from e

    def refresh(self, refresh_token):
        payload = {
            "grant_type": "refresh_token",
            "refresh_token": refresh_token,
            "client_id": self.cfg.client_id,
        }
        if self.cfg.scopes:
            payload["scope"] = self.cfg.scopes
        status, raw = self._request(
            self.cfg.token_url,
            method="POST",
            headers={"Content-Type": "application/json", "User-Agent": self.cfg.user_agent},
            body=json.dumps(payload).encode(),
        )
        if status != 200:
            raise UpstreamError("refresh_failed", status)
        try:
            data = json.loads(raw)
            access = data["access_token"]
        except (ValueError, KeyError, TypeError):
            raise UpstreamError("refresh_bad_response", status)
        new_refresh = data.get("refresh_token") or refresh_token
        expires_in = data.get("expires_in")
        expires_at = int((time.time() + float(expires_in)) * 1000) if isinstance(expires_in, (int, float)) else None
        self.store.save(access, new_refresh, expires_at)
        log.info("oauth token refreshed (expires_in=%s)", expires_in)
        return access

    def _get_usage(self, access_token):
        return self._request(
            self.cfg.usage_url,
            headers={
                "Authorization": "Bearer " + access_token,
                "anthropic-beta": self.cfg.beta,
                "Content-Type": "application/json",
                "User-Agent": self.cfg.user_agent,
            },
        )

    def fetch_usage(self):
        try:
            creds = self.store.load()
        except (OSError, ValueError) as e:
            raise UpstreamError("credentials_unreadable") from e
        access, refresh_token = creds["accessToken"], creds["refreshToken"]
        expires_at = creds["expiresAt"]
        near_expiry = isinstance(expires_at, (int, float)) and expires_at / 1000 - time.time() < self.cfg.refresh_skew
        if (not access or near_expiry) and refresh_token:
            access = self.refresh(refresh_token)
        if not access:
            raise UpstreamError("no_access_token")
        status, raw = self._get_usage(access)
        if status in (401, 403) and refresh_token:
            access = self.refresh(refresh_token)
            status, raw = self._get_usage(access)
        if status != 200:
            raise UpstreamError("usage_http_%d" % status, status)
        try:
            data = json.loads(raw)
        except ValueError:
            raise UpstreamError("usage_bad_json", status)
        if not isinstance(data, dict):
            raise UpstreamError("usage_bad_json", status)
        return data


def _num(v):
    if isinstance(v, bool):
        return None
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, str):
        try:
            return float(v)
        except ValueError:
            return None
    return None


def _parse_time(v):
    if isinstance(v, (int, float)) and not isinstance(v, bool):
        return datetime.fromtimestamp(v / 1000 if v > 1e11 else v, tz=timezone.utc)
    if not isinstance(v, str) or not v:
        return None
    s = v.strip().replace("Z", "+00:00")
    m = re.match(r"^(.*T\d{2}:\d{2}:\d{2})(\.\d+)?(.*)$", s)
    if m:
        frac = (m.group(2) or "")[:7]
        s = m.group(1) + frac + m.group(3)
    try:
        dt = datetime.fromisoformat(s)
    except ValueError:
        return None
    return dt if dt.tzinfo else dt.replace(tzinfo=timezone.utc)


def _window(obj):
    if not isinstance(obj, dict):
        return None
    pct = _num(obj.get("utilization"))
    if pct is None:
        pct = _num(obj.get("percent"))
    return {"pct": pct, "resets_at": obj.get("resets_at")}


def _limits(data):
    rows = data.get("limits")
    return [r for r in rows if isinstance(r, dict)] if isinstance(rows, list) else []


def _scope_model(row):
    scope = row.get("scope")
    model = scope.get("model") if isinstance(scope, dict) else None
    name = model.get("display_name") if isinstance(model, dict) else None
    return name if isinstance(name, str) else ""


def extract_claude(data):
    """Weekly all-model window: seven_day, else limits[] kind=weekly_all."""
    w = _window(data.get("seven_day"))
    if w and w["pct"] is not None:
        return w
    for row in _limits(data):
        if row.get("kind") == "weekly_all" and not _scope_model(row):
            w = _window(row)
            if w and w["pct"] is not None:
                return w
    return None


def extract_fable(data):
    """Fable weekly window: limits[] weekly_scoped model 'Fable', else seven_day_overage_included."""
    for row in _limits(data):
        if row.get("kind") == "weekly_scoped" and "fable" in _scope_model(row).lower():
            w = _window(row)
            if w and w["pct"] is not None:
                return w
    w = _window(data.get("seven_day_overage_included"))
    if w and w["pct"] is not None:
        return w
    return None


def make_item(label, window, tz):
    if not window or window.get("pct") is None:
        return {"label": label, "value": "n/a", "pct": None}
    pct = int(round(min(100.0, max(0.0, window["pct"]))))
    item = {"label": label, "value": "%d%%" % pct, "pct": pct}
    reset = _parse_time(window.get("resets_at"))
    if reset:
        item["resets"] = reset.astimezone(tz).strftime("%m-%d %H:%M")
    return item


def build_items(data, tz):
    return [
        make_item("Claude", extract_claude(data), tz),
        make_item("Fable", extract_fable(data), tz),
    ]


class UsageCache:
    """Single-flight cache so upstream is called at most once per cache window."""

    def __init__(self, cfg, client, clock=time.time):
        self.cfg = cfg
        self.client = client
        self.clock = clock
        self._lock = threading.Lock()
        self.last_attempt = None
        self.last_success = None
        self.items = None
        self.error = None

    def snapshot(self):
        with self._lock:
            now = self.clock()
            if self.last_attempt is None or now - self.last_attempt >= self.cfg.cache_seconds:
                self.last_attempt = now
                try:
                    data = self.client.fetch_usage()
                    self.items = build_items(data, self.cfg.tz)
                    self.last_success = now
                    self.error = None
                except UpstreamError as e:
                    self.error = e.code
                    log.warning("upstream fetch failed: %s (status=%s)", e.code, e.status)
                except Exception as e:
                    self.error = "internal"
                    log.error("unexpected fetch error: %s", mask(repr(e)))
            return self._render()

    def _render(self):
        tz = self.cfg.tz
        body = {
            "updated": None,
            "period": "7d",
            "items": self.items or [
                {"label": "Claude", "value": "n/a", "pct": None},
                {"label": "Fable", "value": "n/a", "pct": None},
            ],
            "status": "ok",
        }
        if self.last_success is not None:
            body["updated"] = datetime.fromtimestamp(self.last_success, tz=tz).isoformat(timespec="seconds")
        if self.error:
            body["status"] = "stale" if self.items else "error"
            body["error"] = self.error
        return body


class AuthLimiter:
    """Locks out a client address after repeated bad clock tokens."""

    MAX_TRACKED = 4096

    def __init__(self, cfg, clock=time.time):
        self.max_failures = cfg.auth_max_failures
        self.window = cfg.auth_window
        self.lockout = cfg.auth_lockout
        self.clock = clock
        self._lock = threading.Lock()
        self._clients = {}

    def retry_after(self, client):
        """Seconds until client may try again, or 0 if it is not locked out."""
        with self._lock:
            entry = self._clients.get(client)
            if not entry or not entry["locked_until"]:
                return 0
            left = entry["locked_until"] - self.clock()
            if left <= 0:
                del self._clients[client]
                return 0
            return int(left) + 1

    def failure(self, client):
        """Records a failed attempt; returns True when this attempt triggered a lockout."""
        with self._lock:
            now = self.clock()
            entry = self._clients.get(client)
            if not entry or now - entry["first"] > self.window:
                self._prune(now)
                entry = self._clients[client] = {"first": now, "count": 0, "locked_until": 0}
            entry["count"] += 1
            if entry["count"] >= self.max_failures:
                entry["locked_until"] = now + self.lockout
                return True
            return False

    def success(self, client):
        with self._lock:
            self._clients.pop(client, None)

    def _prune(self, now):
        if len(self._clients) < self.MAX_TRACKED:
            return
        for key in [k for k, e in self._clients.items()
                    if e["locked_until"] <= now and now - e["first"] > self.window]:
            del self._clients[key]
        while len(self._clients) >= self.MAX_TRACKED:
            del self._clients[next(iter(self._clients))]


def make_handler(cfg, cache, limiter):
    expected = ("Bearer " + cfg.clock_token).encode()

    class Handler(BaseHTTPRequestHandler):
        server_version = "clock-usage-relay/" + VERSION
        sys_version = ""

        def client_ip(self):
            """Peer address, or the last X-Forwarded-For hop when the peer is a trusted proxy."""
            peer = self.client_address[0]
            headers = getattr(self, "headers", None)
            forwarded = headers.get("X-Forwarded-For") if headers else None
            if forwarded and cfg.is_trusted_proxy(peer):
                hop = _parse_ip(forwarded.split(",")[-1])
                if hop is not None:
                    return str(hop)
            return peer

        def _send(self, code, obj, headers=None):
            raw = json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(raw)))
            self.send_header("Cache-Control", "no-store")
            for name, value in (headers or {}).items():
                self.send_header(name, value)
            self.end_headers()
            self.wfile.write(raw)

        def do_GET(self):
            path = self.path.split("?", 1)[0]
            if path == "/healthz":
                return self._send(200, {"ok": True})
            if path != "/clock/usage":
                return self._send(404, {"error": "not_found"})
            client = self.client_ip()
            wait = limiter.retry_after(client)
            if wait:
                return self._send(429, {"error": "too_many_failures"}, {"Retry-After": str(wait)})
            got = (self.headers.get("Authorization") or "").encode()
            if not hmac.compare_digest(got, expected):
                if limiter.failure(client):
                    log.warning("client %s locked out for %ds after %d bad tokens",
                                client, cfg.auth_lockout, cfg.auth_max_failures)
                return self._send(401, {"error": "unauthorized"})
            limiter.success(client)
            self._send(200, cache.snapshot())

        def do_POST(self):
            self._send(405, {"error": "method_not_allowed"})

        do_PUT = do_DELETE = do_PATCH = do_POST

        def log_message(self, fmt, *args):
            log.info("%s %s", self.client_ip(), mask(fmt % args))

    return Handler


def build_server(cfg):
    store = CredentialStore(cfg.credentials_file)
    cache = UsageCache(cfg, AnthropicClient(cfg, store))
    limiter = AuthLimiter(cfg)
    server = ThreadingHTTPServer((cfg.bind, cfg.port), make_handler(cfg, cache, limiter))
    server.daemon_threads = True
    server.limiter = limiter
    return server, cache


def proxy_config_warning(cfg):
    """True when the relay listens off-loopback but trusts no off-host proxy."""
    bind_ip = _parse_ip(cfg.bind)
    if bind_ip is not None and bind_ip.is_loopback:
        return False
    return all(net.network_address.is_loopback and net.broadcast_address.is_loopback
               for net in cfg.trusted_proxies)


def main():
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s", stream=sys.stdout)
    cfg = Config()
    mode = os.stat(cfg.credentials_file).st_mode & 0o077
    if mode:
        log.warning("credentials file is readable by group/others; chmod 600 recommended")
    if proxy_config_warning(cfg):
        log.warning("listening on %s but RELAY_TRUSTED_PROXIES has only loopback; "
                    "add the nginx proxy address or every clock shares one lockout bucket", cfg.bind)
    server, _ = build_server(cfg)
    log.info("listening on %s:%d (cache %ds)", cfg.bind, cfg.port, cfg.cache_seconds)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
