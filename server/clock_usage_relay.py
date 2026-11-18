#!/usr/bin/env python3
"""Smart clock usage relay: serves Claude subscription weekly utilization as a small JSON."""

import hmac
import ipaddress
import json
import logging
import os
import re
import sqlite3
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from bisect import bisect_left
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    from zoneinfo import ZoneInfo
except ImportError:  # pragma: no cover
    ZoneInfo = None

VERSION = "1.3.1"

DEFAULTS = {
    "RELAY_BIND": "127.0.0.1",
    "RELAY_PORT": "8787",
    "RELAY_SAMPLE_RETENTION_DAYS": "14",
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

DEFAULT_SAMPLE_SECONDS = 1800
DEFAULT_STATE_DIR = "/var/lib/clock-usage-relay"
RESET_SHIFT_SECONDS = 3600
HOURLY_BUCKETS = 24

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
        interval = env.get("RELAY_SAMPLE_SECONDS") or env.get("RELAY_CACHE_SECONDS") or DEFAULT_SAMPLE_SECONDS
        self.sample_seconds = max(60, int(interval))
        self.retention_days = max(2, int(get("RELAY_SAMPLE_RETENTION_DAYS")))
        state_dir = (env.get("STATE_DIRECTORY") or DEFAULT_STATE_DIR).split(":")[0]
        self.samples_db = env.get("RELAY_SAMPLES_DB") or os.path.join(state_dir, "samples.db")
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
    if re.fullmatch(r"\s*\d+(\.\d+)?\s*", v):
        return _parse_time(float(v))
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


ITEMS = (("Claude", "claude", extract_claude), ("Fable", "fable", extract_fable))


def _text(v):
    return None if v is None else str(v)


class SampleStore:
    """sqlite table of usage samples, one row per successful upstream call."""

    def __init__(self, path):
        self.path = path
        self._lock = threading.Lock()
        self._db = None

    def _conn(self):
        if self._db is None:
            os.makedirs(os.path.dirname(os.path.abspath(self.path)), mode=0o700, exist_ok=True)
            os.close(os.open(self.path, os.O_RDWR | os.O_CREAT, 0o600))
            os.chmod(self.path, 0o600)
            db = sqlite3.connect(self.path, timeout=5, check_same_thread=False)
            db.execute("PRAGMA temp_store=MEMORY")
            db.execute("CREATE TABLE IF NOT EXISTS samples (ts INTEGER PRIMARY KEY, claude_pct REAL, "
                       "claude_resets_at TEXT, fable_pct REAL, fable_resets_at TEXT, raw_ok INTEGER NOT NULL)")
            db.commit()
            self._db = db
        return self._db

    def _run(self, fn):
        with self._lock:
            try:
                return fn(self._conn())
            except Exception:
                if self._db is not None:
                    self._db.close()
                    self._db = None
                raise

    def add(self, ts, windows, keep_seconds):
        """Stores one sample of (claude, fable) windows and drops rows older than keep_seconds."""
        claude, fable = (w or {} for w in windows)
        row = (int(ts), claude.get("pct"), _text(claude.get("resets_at")),
               fable.get("pct"), _text(fable.get("resets_at")), int(bool(claude or fable)))

        def write(db):
            with db:
                db.execute("INSERT OR REPLACE INTO samples VALUES (?, ?, ?, ?, ?, ?)", row)
                db.execute("DELETE FROM samples WHERE ts < ?", (int(ts - keep_seconds),))
        self._run(write)

    def latest(self):
        rows = self._run(lambda db: db.execute("SELECT * FROM samples ORDER BY ts DESC LIMIT 1").fetchall())
        return _row(rows[0]) if rows else None

    def since(self, ts):
        rows = self._run(lambda db: db.execute("SELECT * FROM samples WHERE ts >= ? ORDER BY ts",
                                               (int(ts),)).fetchall())
        return [_row(r) for r in rows]

    def stats(self):
        return self._run(lambda db: db.execute("SELECT COUNT(*), MAX(ts) FROM samples").fetchone())

    def close(self):
        with self._lock:
            if self._db is not None:
                self._db.close()
                self._db = None


def _row(r):
    return dict(zip(("ts", "claude_pct", "claude_resets_at", "fable_pct", "fable_resets_at", "raw_ok"), r))


def _reset_epoch(text):
    t = _parse_time(text)
    return t.timestamp() if t else None


def increase(points, gap):
    """Rise from the first to the last point with reset correction; None if no point sits within gap before a reset."""
    total, base = 0.0, points[0]["pct"]
    for prev, cur in zip(points, points[1:]):
        old, new = _reset_epoch(prev["resets_at"]), _reset_epoch(cur["resets_at"])
        if old is not None and new is not None and abs(new - old) >= RESET_SHIFT_SECONDS:
            if old - prev["ts"] > gap:
                return None
            total += prev["pct"] - base
            base = 0.0
    return total + points[-1]["pct"] - base


def window_delta(points, seconds, gap):
    """Increase from the latest point back to the newest point at least `seconds` older, or None."""
    if not points:
        return None
    target = points[-1]["ts"] - seconds
    i = bisect_left([p["ts"] for p in points], target + 1) - 1
    if i < 0 or points[i]["ts"] < target - gap:
        return None
    return increase(points[i:], gap)


def hourly_deltas(points, now, tz, gap, hours=HOURLY_BUCKETS):
    """Increase per completed local clock hour, oldest first; None where a bucket edge has no sample."""
    top = datetime.fromtimestamp(now, tz).replace(minute=0, second=0, microsecond=0).timestamp()
    stamps = [p["ts"] for p in points]

    def last_in(k):
        end = top - 3600 * k
        i = bisect_left(stamps, end) - 1
        return i if i >= 0 and stamps[i] >= end - 3600 else None

    out = []
    for k in range(hours - 1, -1, -1):
        a, b = last_in(k + 1), last_in(k)
        out.append(None if a is None or b is None else increase(points[a:b + 1], gap))
    return out


def _r1(v):
    return None if v is None else round(v, 1) + 0.0


class Sampler:
    """Polls upstream on a fixed interval into SampleStore and renders clock responses from stored samples."""

    def __init__(self, cfg, client, store, clock=time.time):
        self.cfg = cfg
        self.client = client
        self.store = store
        self.clock = clock
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self.last_attempt = None
        self.error = None

    def tick(self):
        """Samples upstream once if the interval has elapsed; returns True when it called upstream."""
        with self._lock:
            now = self.clock()
            if self.last_attempt is not None and now - self.last_attempt < self.cfg.sample_seconds:
                return False
            self.last_attempt = now
            try:
                data = self.client.fetch_usage()
                windows = [extract(data) for _, _, extract in ITEMS]
            except UpstreamError as e:
                self.error = e.code
                log.warning("upstream fetch failed: %s (status=%s)", e.code, e.status)
                return True
            except Exception as e:
                self.error = "internal"
                log.error("unexpected fetch error: %s", mask(repr(e)))
                return True
            try:
                self.store.add(now, windows, self.cfg.retention_days * 86400)
                self.error = None
            except Exception as e:
                self.error = "storage_error"
                log.error("sample store failed (%s): %s", self.store.path, mask(repr(e)))
            return True

    def run(self):
        while not self._stop.is_set():
            try:
                self.tick()
                wait = self.cfg.sample_seconds - (self.clock() - self.last_attempt)
            except Exception as e:
                log.error("sampler loop error: %s", mask(repr(e)))
                wait = self.cfg.sample_seconds
            self._stop.wait(min(max(1.0, wait), self.cfg.sample_seconds))

    def start(self):
        thread = threading.Thread(target=self.run, name="sampler", daemon=True)
        thread.start()
        return thread

    def stop(self):
        self._stop.set()

    def health(self):
        body = {"ok": True, "version": VERSION, "samples": None, "latest_sample": None}
        try:
            count, latest = self.store.stats()
        except Exception as e:
            log.error("sample store unreadable: %s", mask(repr(e)))
            return body
        body["samples"] = count
        if latest is not None:
            body["latest_sample"] = datetime.fromtimestamp(latest, tz=self.cfg.tz).isoformat(timespec="seconds")
        return body

    def snapshot(self, hourly=False):
        now = self.clock()
        gap = 2 * self.cfg.sample_seconds
        error = self.error
        try:
            latest = self.store.latest()
            history = self.store.since(now - (HOURLY_BUCKETS + 2) * 3600 - gap) if latest else []
        except Exception as e:
            log.error("sample store unreadable: %s", mask(repr(e)))
            latest, history, error = None, [], "storage_error"
        body = {"updated": None, "period": "7d", "status": "error", "version": VERSION, "sample_age_s": None}
        items = []
        for label, key, _ in ITEMS:
            window = {"pct": latest[key + "_pct"], "resets_at": latest[key + "_resets_at"]} if latest else None
            item = make_item(label, window, self.cfg.tz)
            points = [{"ts": r["ts"], "pct": r[key + "_pct"], "resets_at": r[key + "_resets_at"]}
                      for r in history if r[key + "_pct"] is not None]
            if not (points and points[-1]["ts"] == latest["ts"]):
                points = []
            item["pct_raw"] = window["pct"] if window else None
            item["delta_1h"] = _r1(window_delta(points, 3600, gap))
            item["delta_24h"] = _r1(window_delta(points, 86400, gap))
            if hourly:
                item["hourly"] = [_r1(v) for v in hourly_deltas(points, now, self.cfg.tz, gap)]
            items.append(item)
        if latest:
            age = max(0, int(now - latest["ts"]))
            body["updated"] = datetime.fromtimestamp(latest["ts"], tz=self.cfg.tz).isoformat(timespec="seconds")
            body["sample_age_s"] = age
            body["status"] = "ok" if age <= gap else "stale"
        elif not error:
            error = "no_sample"
        body["items"] = items
        if error:
            body["error"] = error
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


def make_handler(cfg, sampler, limiter):
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
            path, _, query = self.path.partition("?")
            if path == "/healthz":
                return self._send(200, sampler.health())
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
            hourly = "1" in urllib.parse.parse_qs(query).get("hourly", [])
            self._send(200, sampler.snapshot(hourly=hourly))

        def do_POST(self):
            self._send(405, {"error": "method_not_allowed"})

        do_PUT = do_DELETE = do_PATCH = do_POST

        def log_message(self, fmt, *args):
            log.info("%s %s", self.client_ip(), mask(fmt % args))

    return Handler


def build_server(cfg):
    sampler = Sampler(cfg, AnthropicClient(cfg, CredentialStore(cfg.credentials_file)), SampleStore(cfg.samples_db))
    limiter = AuthLimiter(cfg)
    server = ThreadingHTTPServer((cfg.bind, cfg.port), make_handler(cfg, sampler, limiter))
    server.daemon_threads = True
    server.limiter = limiter
    return server, sampler


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
    server, sampler = build_server(cfg)
    log.info("listening on %s:%d (sample every %ds into %s)", cfg.bind, cfg.port, cfg.sample_seconds, cfg.samples_db)
    sampler.start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
