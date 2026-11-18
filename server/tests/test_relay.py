import json
import os
import sqlite3
import stat
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import clock_usage_relay as relay  # noqa: E402

CLOCK_TOKEN = "clock-test-token-0123456789"
OLD_ACCESS = "sk-ant-oat01-OLDACCESS-xxxxxxxxxxxxxxxx"
NEW_ACCESS = "sk-ant-oat01-NEWACCESS-yyyyyyyyyyyyyyyy"
OLD_REFRESH = "sk-ant-ort01-OLDREFRESH-zzzzzzzzzzzzzz"
NEW_REFRESH = "sk-ant-ort01-NEWREFRESH-wwwwwwwwwwwwww"

SAMPLE_USAGE = {
    "five_hour": {"utilization": 12.0, "resets_at": "2026-11-13T06:00:00.123456+00:00"},
    "seven_day": {"utilization": 42.4, "resets_at": "2026-11-18T00:00:00Z"},
    "seven_day_opus": None,
    "seven_day_sonnet": {"utilization": 3, "resets_at": None},
    "extra_usage": {"is_enabled": False, "monthly_limit": None, "used_credits": None, "utilization": None},
    "limits": [
        {"kind": "session", "group": "session", "percent": 12, "resets_at": "2026-11-13T06:00:00Z",
         "severity": "normal", "is_active": False, "scope": None},
        {"kind": "weekly_all", "group": "weekly", "percent": 42.4, "resets_at": "2026-11-18T00:00:00Z",
         "severity": "normal", "is_active": True, "scope": None},
        {"kind": "weekly_scoped", "group": "weekly", "percent": 16.6, "resets_at": "2026-11-18T00:00:00Z",
         "severity": "normal", "is_active": False, "scope": {"model": {"display_name": "Fable"}}},
    ],
}


class MockUpstream:
    """In-process stand-in for the usage and token endpoints."""

    def __init__(self):
        self.usage_calls = 0
        self.token_calls = 0
        self.usage_body = SAMPLE_USAGE
        self.valid_access = {OLD_ACCESS}
        self.token_status = 200
        self.usage_status = None
        self.last_token_request = None
        self.last_usage_headers = None
        mock = self

        class H(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def _json(self, code, obj):
                raw = json.dumps(obj).encode()
                self.send_response(code)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)

            def do_GET(self):
                mock.usage_calls += 1
                mock.last_usage_headers = self.headers
                if mock.usage_status:
                    return self._json(mock.usage_status, {"error": "x"})
                auth = self.headers.get("Authorization", "")
                if auth[len("Bearer "):] not in mock.valid_access:
                    return self._json(401, {"error": "invalid token"})
                self._json(200, mock.usage_body)

            def do_POST(self):
                mock.token_calls += 1
                n = int(self.headers.get("Content-Length", "0"))
                mock.last_token_request = json.loads(self.rfile.read(n))
                if mock.token_status != 200:
                    return self._json(mock.token_status, {"error": "invalid_grant"})
                mock.valid_access = {NEW_ACCESS}
                self._json(200, {"access_token": NEW_ACCESS, "refresh_token": NEW_REFRESH,
                                 "expires_in": 28800, "token_type": "Bearer"})

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), H)
        self.url = "http://127.0.0.1:%d" % self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()


class RelayTestBase(unittest.TestCase):
    def setUp(self):
        self.mock = MockUpstream()
        self.dir = tempfile.mkdtemp(prefix="relaytest-")
        self.creds = os.path.join(self.dir, "credentials.json")
        self.write_creds(OLD_ACCESS, OLD_REFRESH, int((time.time() + 3600) * 1000))
        self.env = env = {
            "RELAY_BIND": "127.0.0.1",
            "RELAY_PORT": "0",
            "RELAY_CACHE_SECONDS": "300",
            "RELAY_USAGE_URL": self.mock.url + "/api/oauth/usage",
            "RELAY_TOKEN_URL": self.mock.url + "/v1/oauth/token",
            "RELAY_CREDENTIALS_FILE": self.creds,
            "RELAY_CLOCK_TOKEN": CLOCK_TOKEN,
            "RELAY_HTTP_TIMEOUT": "5",
            "RELAY_SAMPLES_DB": os.path.join(self.dir, "state", "samples.db"),
        }
        self.cfg = relay.Config(env)
        self.server, self.sampler = relay.build_server(self.cfg)
        self.now = [1_000_000.0]
        self.sampler.clock = lambda: self.now[0]
        self.server.limiter.clock = lambda: self.now[0]
        self.base = "http://127.0.0.1:%d" % self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.sampler.store.close()
        self.mock.close()

    def write_creds(self, access, refresh, expires_at):
        with open(self.creds, "w") as f:
            json.dump({"claudeAiOauth": {"accessToken": access, "refreshToken": refresh,
                                         "expiresAt": expires_at, "scopes": ["user:profile"]}}, f)
        os.chmod(self.creds, 0o600)

    def fetch(self, path="/clock/usage"):
        self.sampler.tick()
        return self.get(path)

    def get(self, path="/clock/usage", token=CLOCK_TOKEN, headers=None):
        req = urllib.request.Request(self.base + path)
        if token is not None:
            req.add_header("Authorization", "Bearer " + token)
        for name, value in (headers or {}).items():
            req.add_header(name, value)
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                return r.status, r.read().decode()
        except urllib.error.HTTPError as e:
            self.last_headers = e.headers
            return e.code, e.read().decode()


class EndpointTests(RelayTestBase):
    def test_contract_shape_and_values(self):
        code, raw = self.fetch()
        self.assertEqual(code, 200)
        body = json.loads(raw)
        self.assertEqual(body["period"], "7d")
        self.assertEqual(body["status"], "ok")
        self.assertTrue(body["updated"].endswith("+09:00"))
        legacy = [{k: i[k] for k in ("label", "value", "pct", "resets")} for i in body["items"]]
        self.assertEqual(legacy[0], {"label": "Claude", "value": "42%", "pct": 42, "resets": "11-18 09:00"})
        self.assertEqual(legacy[1], {"label": "Fable", "value": "17%", "pct": 17, "resets": "11-18 09:00"})
        self.assertEqual(self.mock.last_usage_headers.get("anthropic-beta"), "oauth-2025-04-20")

    def test_oauth_tokens_never_reach_clock(self):
        _, raw = self.fetch()
        for secret in (OLD_ACCESS, OLD_REFRESH, NEW_ACCESS, NEW_REFRESH, "sk-ant"):
            self.assertNotIn(secret, raw)

    def test_rejects_missing_or_wrong_token(self):
        self.assertEqual(self.get(token=None)[0], 401)
        self.assertEqual(self.get(token="wrong-token-wrong-token")[0], 401)
        self.assertEqual(self.mock.usage_calls, 0)

    def test_unknown_path_and_methods(self):
        self.assertEqual(self.get("/other")[0], 404)
        self.assertEqual(self.get("/healthz", token=None)[0], 200)
        req = urllib.request.Request(self.base + "/clock/usage", data=b"x", method="POST")
        with self.assertRaises(urllib.error.HTTPError) as cm:
            urllib.request.urlopen(req, timeout=5)
        self.assertEqual(cm.exception.code, 405)

    def test_sample_interval_limits_upstream_calls(self):
        for _ in range(10):
            self.assertEqual(self.fetch()[0], 200)
        self.assertEqual(self.mock.usage_calls, 1)
        self.now[0] += 299
        self.fetch()
        self.assertEqual(self.mock.usage_calls, 1)
        self.now[0] += 2
        self.fetch()
        self.assertEqual(self.mock.usage_calls, 2)

    def test_concurrent_ticks_single_upstream_call(self):
        threads = [threading.Thread(target=self.fetch) for _ in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(self.mock.usage_calls, 1)


class RefreshTests(RelayTestBase):
    def test_401_triggers_refresh_and_persists_rotated_tokens(self):
        self.mock.valid_access = {"something-else"}
        code, raw = self.fetch()
        self.assertEqual(code, 200)
        self.assertEqual(json.loads(raw)["items"][0]["pct"], 42)
        self.assertEqual(self.mock.token_calls, 1)
        req = self.mock.last_token_request
        self.assertEqual(req["grant_type"], "refresh_token")
        self.assertEqual(req["refresh_token"], OLD_REFRESH)
        self.assertEqual(req["client_id"], "9d1c250a-e61b-44d9-88ed-5944d1962f5e")
        with open(self.creds) as f:
            saved = json.load(f)["claudeAiOauth"]
        self.assertEqual(saved["accessToken"], NEW_ACCESS)
        self.assertEqual(saved["refreshToken"], NEW_REFRESH)
        self.assertEqual(saved["scopes"], ["user:profile"])
        self.assertGreater(saved["expiresAt"], time.time() * 1000)
        self.assertEqual(stat.S_IMODE(os.stat(self.creds).st_mode), 0o600)

    def test_near_expiry_refreshes_proactively(self):
        self.write_creds(OLD_ACCESS, OLD_REFRESH, int((time.time() + 60) * 1000))
        self.assertEqual(self.fetch()[0], 200)
        self.assertEqual(self.mock.token_calls, 1)
        self.assertEqual(self.mock.usage_calls, 1)

    def test_refresh_failure_serves_last_value_with_error(self):
        self.fetch()
        self.mock.valid_access = set()
        self.mock.token_status = 400
        self.now[0] += 301
        body = json.loads(self.fetch()[1])
        self.assertEqual((body["status"], body["error"]), ("ok", "refresh_failed"))
        self.now[0] += 300
        code, raw = self.fetch()
        body = json.loads(raw)
        self.assertEqual(code, 200)
        self.assertEqual(body["status"], "stale")
        self.assertEqual(body["error"], "refresh_failed")
        self.assertEqual(body["items"][0]["pct"], 42)

    def test_failure_before_any_success(self):
        self.mock.usage_status = 500
        code, raw = self.fetch()
        body = json.loads(raw)
        self.assertEqual(code, 200)
        self.assertEqual(body["status"], "error")
        self.assertEqual(body["error"], "usage_http_500")
        self.assertIsNone(body["updated"])
        self.assertEqual([i["value"] for i in body["items"]], ["n/a", "n/a"])

    def test_failure_is_also_rate_limited(self):
        self.mock.usage_status = 500
        for _ in range(5):
            self.fetch()
        self.assertEqual(self.mock.usage_calls, 1)

    def test_unreadable_credentials(self):
        os.unlink(self.creds)
        body = json.loads(self.fetch()[1])
        self.assertEqual(body["error"], "credentials_unreadable")


class AuthLimitTests(RelayTestBase):
    def bad(self, n, headers=None):
        return [self.get(token="wrong-token-wrong-token", headers=headers)[0] for _ in range(n)]

    def test_lockout_after_repeated_bad_tokens(self):
        self.assertEqual(self.bad(10), [401] * 10)
        code, raw = self.get()
        self.assertEqual(code, 429)
        self.assertEqual(json.loads(raw), {"error": "too_many_failures"})
        self.assertEqual(self.last_headers.get("Retry-After"), "901")
        self.assertEqual(self.mock.usage_calls, 0)

    def test_lockout_expires(self):
        self.bad(10)
        self.now[0] += 899
        self.assertEqual(self.get()[0], 429)
        self.now[0] += 2
        self.assertEqual(self.get()[0], 200)

    def test_success_resets_and_window_expires(self):
        self.bad(9)
        self.assertEqual(self.get()[0], 200)
        self.assertEqual(self.bad(9), [401] * 9)
        self.now[0] += 601
        self.assertEqual(self.bad(9), [401] * 9)
        self.assertEqual(self.get()[0], 200)

    def test_forwarded_client_from_trusted_proxy(self):
        attacker = {"X-Forwarded-For": "198.51.100.7"}
        clock = {"X-Forwarded-For": "203.0.113.9"}
        self.bad(10, attacker)
        self.assertEqual(self.get(headers=attacker)[0], 429)
        self.assertEqual(self.get(headers=clock)[0], 200)
        self.assertEqual(self.get(headers={"X-Forwarded-For": "1.2.3.4, 198.51.100.7"})[0], 429)

    def test_garbage_forwarded_hop_falls_back_to_peer(self):
        self.bad(10, {"X-Forwarded-For": "not-an-ip"})
        self.assertEqual(self.get(headers={"X-Forwarded-For": "also-garbage"})[0], 429)
        self.assertEqual(self.get(headers={"X-Forwarded-For": "203.0.113.9"})[0], 200)

    def test_forwarded_header_ignored_from_untrusted_peer(self):
        cfg = relay.Config(dict(self.env, RELAY_TRUSTED_PROXIES="192.0.2.1"))
        server, sampler = relay.build_server(cfg)
        base = "http://127.0.0.1:%d" % server.server_address[1]
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            def call(token, xff):
                req = urllib.request.Request(base + "/clock/usage")
                req.add_header("Authorization", "Bearer " + token)
                req.add_header("X-Forwarded-For", xff)
                try:
                    with urllib.request.urlopen(req, timeout=10) as r:
                        return r.status
                except urllib.error.HTTPError as e:
                    return e.code
            for i in range(10):
                call("wrong-token-wrong-token", "198.51.100.%d" % i)
            self.assertEqual(call(CLOCK_TOKEN, "203.0.113.9"), 429)
        finally:
            server.shutdown()
            server.server_close()
            sampler.store.close()

    def test_tracked_clients_are_bounded(self):
        limiter = self.server.limiter
        limiter.MAX_TRACKED = 50
        for i in range(200):
            limiter.failure("10.0.%d.%d" % (i // 256, i % 256))
        self.assertLessEqual(len(limiter._clients), 50)


class ParserTests(unittest.TestCase):
    tz = relay._load_tz("Asia/Seoul")

    def items(self, data):
        return relay.build_items(data, self.tz)

    def test_fable_absent_is_na(self):
        data = {"seven_day": {"utilization": 5, "resets_at": None}}
        claude, fable = self.items(data)
        self.assertEqual(claude, {"label": "Claude", "value": "5%", "pct": 5})
        self.assertEqual(fable, {"label": "Fable", "value": "n/a", "pct": None})

    def test_fable_from_overage_included_bucket(self):
        data = {"seven_day_overage_included": {"utilization": 33.5, "resets_at": "2026-11-18T00:00:00+00:00"}}
        _, fable = self.items(data)
        self.assertEqual(fable["pct"], 34)

    def test_claude_from_limits_when_seven_day_missing(self):
        data = {"limits": [{"kind": "weekly_all", "percent": 7, "resets_at": None}]}
        self.assertEqual(self.items(data)[0]["pct"], 7)

    def test_garbage_does_not_crash(self):
        for data in ({}, {"seven_day": "x"}, {"seven_day": {"utilization": "abc"}},
                     {"limits": "nope"}, {"limits": [None, 1, {"kind": "weekly_scoped", "scope": "x"}]},
                     {"seven_day": {"utilization": True}}, {"seven_day": {"utilization": 50, "resets_at": "bad"}}):
            items = self.items(data)
            self.assertEqual(len(items), 2)

    def test_clamps_out_of_range(self):
        self.assertEqual(self.items({"seven_day": {"utilization": 130}})[0]["pct"], 100)
        self.assertEqual(self.items({"seven_day": {"utilization": -3}})[0]["pct"], 0)

    def test_epoch_resets(self):
        item = self.items({"seven_day": {"utilization": 1, "resets_at": 1794960000}})[0]
        self.assertEqual(item["resets"], "11-18 09:00")

    def test_mask(self):
        self.assertNotIn("OLDACCESS", relay.mask("Bearer " + OLD_ACCESS))
        self.assertNotIn("OLDACCESS", relay.mask("token=" + OLD_ACCESS))


class ConfigTests(unittest.TestCase):
    def test_requires_secrets(self):
        with self.assertRaises(SystemExit):
            relay.Config({"RELAY_CREDENTIALS_FILE": "/x"})
        with self.assertRaises(SystemExit):
            relay.Config({"RELAY_CLOCK_TOKEN": CLOCK_TOKEN})

    def test_defaults_bind_loopback_and_trust_local_proxy(self):
        cfg = relay.Config({"RELAY_CREDENTIALS_FILE": "/x", "RELAY_CLOCK_TOKEN": CLOCK_TOKEN})
        self.assertEqual(cfg.bind, "127.0.0.1")
        self.assertTrue(cfg.is_trusted_proxy("127.0.0.1"))
        self.assertTrue(cfg.is_trusted_proxy("::1"))
        self.assertFalse(cfg.is_trusted_proxy("10.0.0.5"))
        self.assertEqual((cfg.auth_max_failures, cfg.auth_window, cfg.auth_lockout), (10, 600, 900))
        self.assertFalse(relay.proxy_config_warning(cfg))

    def test_trusted_proxies_accept_cidr_and_ipv4_mapped_peers(self):
        cfg = relay.Config({"RELAY_CREDENTIALS_FILE": "/x", "RELAY_CLOCK_TOKEN": CLOCK_TOKEN,
                            "RELAY_TRUSTED_PROXIES": "10.0.0.0/24, 192.0.2.10, fd00::/8"})
        self.assertTrue(cfg.is_trusted_proxy("10.0.0.77"))
        self.assertTrue(cfg.is_trusted_proxy("::ffff:10.0.0.77"))
        self.assertTrue(cfg.is_trusted_proxy("192.0.2.10"))
        self.assertTrue(cfg.is_trusted_proxy("fd12::1"))
        self.assertFalse(cfg.is_trusted_proxy("10.0.1.1"))
        self.assertFalse(cfg.is_trusted_proxy("not-an-ip"))

    def test_invalid_trusted_proxy_is_rejected(self):
        with self.assertRaises(SystemExit):
            relay.Config({"RELAY_CREDENTIALS_FILE": "/x", "RELAY_CLOCK_TOKEN": CLOCK_TOKEN,
                          "RELAY_TRUSTED_PROXIES": "10.0.0.0/33"})

    def test_warns_when_listening_off_loopback_without_remote_proxy(self):
        base = {"RELAY_CREDENTIALS_FILE": "/x", "RELAY_CLOCK_TOKEN": CLOCK_TOKEN, "RELAY_BIND": "0.0.0.0"}
        self.assertTrue(relay.proxy_config_warning(relay.Config(base)))
        cfg = relay.Config(dict(base, RELAY_TRUSTED_PROXIES="192.0.2.10"))
        self.assertFalse(relay.proxy_config_warning(cfg))

    def test_token_from_file(self):
        with tempfile.NamedTemporaryFile("w", delete=False) as f:
            f.write(CLOCK_TOKEN + "\n")
        cfg = relay.Config({"RELAY_CREDENTIALS_FILE": "/x", "RELAY_CLOCK_TOKEN_FILE": f.name})
        self.assertEqual(cfg.clock_token, CLOCK_TOKEN)
        os.unlink(f.name)


KST = relay._load_tz("Asia/Seoul")
NOW_KST = 1794979500.0  # 2026-11-18 14:25:00 +09:00
RESET_A_EPOCH = 1794960000.0
RESET_A = "2026-11-18T00:00:00Z"
RESET_B = "2026-11-25T00:00:00Z"


class SampleTests(RelayTestBase):
    def seed(self, rows):
        """Writes (seconds_before_now, claude_pct, claude_resets_at) rows straight into the store."""
        for ago, pct, resets in rows:
            win = {"pct": pct, "resets_at": resets}
            self.sampler.store.add(self.now[0] - ago, [win, {"pct": 17.0, "resets_at": resets}], 14 * 86400)

    def body(self, path="/clock/usage"):
        code, raw = self.get(path)
        self.assertEqual(code, 200)
        return json.loads(raw)

    def rows(self):
        db = sqlite3.connect(self.cfg.samples_db)
        try:
            return db.execute("SELECT * FROM samples ORDER BY ts").fetchall()
        finally:
            db.close()

    def test_sample_saved_as_float_and_old_rows_pruned(self):
        self.sampler.store.add(self.now[0] - 15 * 86400, [{"pct": 1.0}, None], 10 ** 9)
        self.sampler.store.add(self.now[0] - 13 * 86400, [{"pct": 2.0}, None], 10 ** 9)
        self.sampler.tick()
        rows = self.rows()
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0][0], int(self.now[0] - 13 * 86400))
        self.assertEqual(rows[1], (int(self.now[0]), 42.4, RESET_A, 16.6, RESET_A, 1))

    def test_failed_call_writes_no_sample(self):
        self.mock.usage_status = 500
        self.sampler.tick()
        self.assertEqual(self.sampler.store.stats(), (0, None))
        self.assertEqual(self.rows(), [])

    def test_unreadable_format_is_stored_with_raw_ok_zero(self):
        self.mock.usage_body = {"something": "new"}
        self.sampler.tick()
        self.assertEqual(self.rows(), [(int(self.now[0]), None, None, None, None, 0)])
        body = self.body()
        self.assertEqual([i["value"] for i in body["items"]], ["n/a", "n/a"])
        self.assertIsNone(body["items"][0]["delta_1h"])

    def test_samples_survive_restart(self):
        self.sampler.tick()
        self.server.shutdown()
        self.server.server_close()
        self.sampler.store.close()
        self.server, self.sampler = relay.build_server(self.cfg)
        self.sampler.clock = lambda: self.now[0] + 120
        self.base = "http://127.0.0.1:%d" % self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        body = self.body()
        self.assertEqual((body["status"], body["sample_age_s"], body["items"][0]["pct"]), ("ok", 120, 42))
        self.assertNotIn("error", body)
        self.assertEqual(self.mock.usage_calls, 1)

    def test_clock_requests_never_call_upstream(self):
        for path in ("/clock/usage", "/clock/usage?hourly=1") * 5:
            self.get(path)
        self.assertEqual(self.mock.usage_calls, 0)
        self.sampler.tick()
        for _ in range(10):
            self.get()
        self.assertEqual(self.mock.usage_calls, 1)

    def test_status_follows_sample_age(self):
        body = self.body()
        self.assertEqual((body["status"], body["error"], body["sample_age_s"]), ("error", "no_sample", None))
        self.sampler.tick()
        self.assertEqual(self.body()["status"], "ok")
        self.now[0] += 600
        self.assertEqual(self.body()["status"], "ok")
        self.now[0] += 1
        body = self.body()
        self.assertEqual((body["status"], body["sample_age_s"], body["items"][0]["pct"]), ("stale", 601, 42))

    def test_pct_stays_int_and_pct_raw_keeps_float(self):
        self.sampler.tick()
        code, raw = self.get()
        self.assertIn('"pct":42,', raw)
        self.assertIn('"pct_raw":42.4,', raw)
        body = json.loads(raw)
        self.assertEqual(body["version"], "1.3.1")
        self.assertEqual([(i["pct"], i["pct_raw"]) for i in body["items"]], [(42, 42.4), (17, 16.6)])
        self.assertIsInstance(body["items"][0]["pct"], int)

    def test_epoch_resets_round_trip_through_store(self):
        self.mock.usage_body = {"seven_day": {"utilization": 1, "resets_at": 1794960000}}
        self.sampler.tick()
        self.assertEqual(self.body()["items"][0]["resets"], "11-18 09:00")

    def test_hourly_only_on_request(self):
        self.sampler.tick()
        for path in ("/clock/usage", "/clock/usage?hourly=0", "/clock/usage?x=1"):
            self.assertTrue(all("hourly" not in i for i in self.body(path)["items"]), path)
        items = self.body("/clock/usage?hourly=1")["items"]
        self.assertEqual([len(i["hourly"]) for i in items], [24, 24])

    def test_hourly_response_stays_under_clock_body_limit(self):
        self.now[0] = NOW_KST
        rows = [(ago, 0.1 if (ago // 3600) % 2 else 99.9, RESET_A) for ago in range(27 * 3600, -1, -300)]
        self.seed(rows)
        code, raw = self.get("/clock/usage?hourly=1")
        body = json.loads(raw)
        self.assertEqual(body["items"][0]["hourly"].count(-99.8), 12)
        size = len(raw.encode())
        self.assertLess(size, 4096)
        self.assertLess(len(self.get()[1].encode()), size)

    def test_delta_1h_and_24h(self):
        self.seed([(ago, 50.0 - ago / 3000, RESET_A) for ago in range(25 * 3600, -1, -300)])
        item = self.body()["items"][0]
        self.assertEqual((item["delta_1h"], item["delta_24h"]), (1.2, 28.8))
        self.assertEqual(self.body()["items"][1]["delta_1h"], 0.0)

    def test_delta_null_without_comparison_sample(self):
        self.seed([(ago, 10.0, RESET_A) for ago in range(1800, -1, -300)])
        item = self.body()["items"][0]
        self.assertEqual((item["delta_1h"], item["delta_24h"]), (None, None))

    def test_delta_null_when_comparison_sample_too_old(self):
        self.seed([(3 * 86400, 10.0, RESET_A), (0, 12.0, RESET_A)])
        self.assertIsNone(self.body()["items"][0]["delta_1h"])

    def test_delta_negative_kept_without_reset(self):
        self.seed([(3600, 50.0, RESET_A), (0, 48.5, RESET_A)])
        self.assertEqual(self.body()["items"][0]["delta_1h"], -1.5)

    def test_delta_across_reset(self):
        self.now[0] = RESET_A_EPOCH + 1500
        self.seed([(3600, 90.0, RESET_A), (1800, 95.0, RESET_A), (1200, 1.0, RESET_B), (0, 3.0, RESET_B)])
        self.assertEqual(self.body()["items"][0]["delta_1h"], 8.0)

    def test_delta_across_reset_without_pre_reset_sample_is_null(self):
        self.now[0] = RESET_A_EPOCH + 60
        self.seed([(3600, 90.0, RESET_A), (0, 3.0, RESET_B)])
        self.assertIsNone(self.body()["items"][0]["delta_1h"])

    def test_hourly_buckets_on_local_clock_hours(self):
        self.now[0] = NOW_KST
        top = NOW_KST - 25 * 60
        gap_hour = (top - 4 * 3600, top - 3 * 3600)
        rows = [(NOW_KST - t, (t - top) / 3600 + 30.0, RESET_A)
                for t in range(int(top - 26 * 3600), int(NOW_KST) + 1, 300) if not gap_hour[0] <= t < gap_hour[1]]
        self.seed(rows)
        hourly = self.body("/clock/usage?hourly=1")["items"][0]["hourly"]
        self.assertEqual(len(hourly), 24)
        self.assertEqual(hourly[:20] + hourly[22:], [1.0] * 22)
        self.assertEqual(hourly[20:22], [None, None])
        self.assertEqual(datetime_kst(top - 3600 * 4), "10:00")

    def test_default_1800s_samples_fill_deltas_and_hourly(self):
        self.cfg.sample_seconds = relay.DEFAULT_SAMPLE_SECONDS
        self.now[0] = NOW_KST
        top = NOW_KST - 25 * 60
        self.seed([(ago, (NOW_KST - ago - top) / 3600 + 30.0, RESET_A) for ago in range(26 * 3600, -1, -1800)])
        item = self.body("/clock/usage?hourly=1")["items"][0]
        self.assertEqual((item["delta_1h"], item["delta_24h"]), (1.0, 24.0))
        self.assertEqual(item["hourly"], [1.0] * 24)

    def test_stale_after_twice_default_interval(self):
        self.cfg.sample_seconds = relay.DEFAULT_SAMPLE_SECONDS
        self.sampler.tick()
        self.now[0] += 3600
        self.assertEqual(self.body()["status"], "ok")
        self.now[0] += 1
        self.assertEqual(self.body()["status"], "stale")

    def test_sampler_exceptions_never_escape(self):
        class Boom:
            def __init__(self, exc):
                self.exc = exc

            def fetch_usage(self):
                raise self.exc

        for exc, code in ((RuntimeError("x"), "internal"), (KeyError("seven_day"), "internal"),
                          (relay.UpstreamError("network"), "network"), (OSError("disk"), "internal")):
            self.sampler.client = Boom(exc)
            self.now[0] += 300
            self.assertTrue(self.sampler.tick())
            self.assertEqual(self.sampler.error, code)

    def test_sampler_loop_survives_tick_errors(self):
        calls = []

        def tick():
            calls.append(1)
            raise RuntimeError("boom")

        class Stop:
            def is_set(self):
                return len(calls) >= 3

            def wait(self, seconds):
                pass

        self.sampler.tick = tick
        self.sampler._stop = Stop()
        thread = threading.Thread(target=self.sampler.run)
        thread.start()
        thread.join(5)
        self.assertFalse(thread.is_alive())
        self.assertEqual(len(calls), 3)

    def test_storage_failure_is_reported_not_raised(self):
        self.sampler.store.close()
        self.sampler.store.path = os.path.join(self.creds, "nested", "samples.db")
        self.sampler.tick()
        self.assertEqual(self.sampler.error, "storage_error")
        body = self.body()
        self.assertEqual((body["status"], body["error"]), ("error", "storage_error"))
        self.assertEqual(self.get("/healthz", token=None)[0], 200)

    def test_healthz_reports_sample_count_without_usage(self):
        health = self.body("/healthz")
        self.assertEqual((health["ok"], health["samples"], health["latest_sample"]), (True, 0, None))
        self.sampler.tick()
        health = self.body("/healthz")
        self.assertEqual(health["samples"], 1)
        self.assertTrue(health["latest_sample"].endswith("+09:00"))
        self.assertFalse({"items", "pct", "pct_raw"} & set(health))


def datetime_kst(ts):
    return relay.datetime.fromtimestamp(ts, KST).strftime("%H:%M")


class StateDirectoryTests(unittest.TestCase):
    UNIT = os.path.join(os.path.dirname(__file__), "..", "deploy", "clock-usage-relay.service")
    BASE = {"RELAY_CREDENTIALS_FILE": "/x", "RELAY_CLOCK_TOKEN": CLOCK_TOKEN}

    def test_default_db_is_inside_unit_state_directory(self):
        with open(self.UNIT) as f:
            unit = dict(l.strip().split("=", 1) for l in f if "=" in l)
        self.assertEqual(unit["ProtectSystem"], "strict")
        self.assertEqual(unit["StateDirectoryMode"], "0700")
        state = "/var/lib/" + unit["StateDirectory"]
        self.assertEqual(relay.Config(self.BASE).samples_db, state + "/samples.db")
        self.assertEqual(relay.Config(dict(self.BASE, STATE_DIRECTORY=state)).samples_db, state + "/samples.db")

    def test_writes_only_inside_state_directory(self):
        root = tempfile.mkdtemp(prefix="relayro-")
        state = os.path.join(root, "state")
        os.mkdir(state, 0o700)
        os.chmod(root, 0o500)
        try:
            cfg = relay.Config(dict(self.BASE, STATE_DIRECTORY=state))
            store = relay.SampleStore(cfg.samples_db)
            for i in range(3):
                store.add(1_000_000 + i * 300, [{"pct": 1.5, "resets_at": RESET_A}, None], 86400)
            self.assertEqual(store.stats(), (3, 1_000_600))
            store.close()
            self.assertEqual(os.listdir(root), ["state"])
            self.assertEqual(os.listdir(state), ["samples.db"])
            self.assertEqual(stat.S_IMODE(os.stat(cfg.samples_db).st_mode), 0o600)
            self.assertEqual(stat.S_IMODE(os.stat(state).st_mode), 0o700)
        finally:
            os.chmod(root, 0o700)

    def test_creates_missing_db_directory_private(self):
        root = tempfile.mkdtemp(prefix="relaydb-")
        store = relay.SampleStore(os.path.join(root, "new", "samples.db"))
        store.stats()
        store.close()
        self.assertEqual(stat.S_IMODE(os.stat(os.path.join(root, "new")).st_mode), 0o700)


class DeltaMathTests(unittest.TestCase):
    def test_reset_correction_sums_both_segments(self):
        pts = [{"ts": 0, "pct": 90.0, "resets_at": "1000"}, {"ts": 900, "pct": 96.0, "resets_at": "1000"},
               {"ts": 1200, "pct": 0.5, "resets_at": "605800"}, {"ts": 3600, "pct": 2.0, "resets_at": "605800"}]
        self.assertEqual(relay.increase(pts, 600), 8.0)
        self.assertIsNone(relay.increase([pts[0]] + pts[2:], 600))

    def test_small_resets_at_jitter_is_not_a_reset(self):
        pts = [{"ts": 0, "pct": 40.0, "resets_at": "2026-11-18T00:00:00.123Z"},
               {"ts": 300, "pct": 41.0, "resets_at": "2026-11-18T00:00:01.456Z"}]
        self.assertEqual(relay.increase(pts, 600), 1.0)

    def test_window_delta_uses_newest_sample_at_least_window_old(self):
        pts = [{"ts": t, "pct": float(t // 300), "resets_at": None} for t in range(0, 7201, 300)]
        self.assertEqual(relay.window_delta(pts, 3600, 600), 12.0)
        self.assertEqual(relay.window_delta(pts[:-1] + [{"ts": 7400, "pct": 30.0, "resets_at": None}], 3600, 600), 18.0)

    def test_1800s_samples_cover_every_clock_hour_at_any_phase(self):
        now = NOW_KST
        for phase in (0, 1, 900, 1799):
            pts = [{"ts": t, "pct": t / 3600.0, "resets_at": None}
                   for t in range(int(now - 27 * 3600) + phase, int(now) + 1, 1800)]
            with self.subTest(phase=phase):
                self.assertTrue(all(v is not None for v in relay.hourly_deltas(pts, now, KST, 3600)))
                self.assertIsNotNone(relay.window_delta(pts, 3600, 3600))
                self.assertIsNotNone(relay.window_delta(pts, 86400, 3600))


class SampleConfigTests(unittest.TestCase):
    BASE = {"RELAY_CREDENTIALS_FILE": "/x", "RELAY_CLOCK_TOKEN": CLOCK_TOKEN}

    def test_sample_interval_and_retention(self):
        self.assertEqual(relay.Config(self.BASE).sample_seconds, 1800)
        self.assertEqual(relay.Config(dict(self.BASE, RELAY_CACHE_SECONDS="600")).sample_seconds, 600)
        cfg = relay.Config(dict(self.BASE, RELAY_CACHE_SECONDS="600", RELAY_SAMPLE_SECONDS="120"))
        self.assertEqual(cfg.sample_seconds, 120)
        self.assertEqual(relay.Config(dict(self.BASE, RELAY_SAMPLE_SECONDS="10")).sample_seconds, 60)
        self.assertEqual(relay.Config(self.BASE).retention_days, 14)
        self.assertEqual(relay.Config(dict(self.BASE, RELAY_SAMPLE_RETENTION_DAYS="1")).retention_days, 2)
        self.assertEqual(relay.Config(dict(self.BASE, RELAY_SAMPLES_DB="/tmp/s.db")).samples_db, "/tmp/s.db")


if __name__ == "__main__":
    unittest.main()
