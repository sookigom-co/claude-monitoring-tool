"""End-to-end check of relay behind nginx; driven by run_nginx_it.sh."""
import io
import json
import logging
import os
import ssl
import subprocess
import sys
import threading
import time
T = os.environ["T"]; REPO = os.environ["REPO"]; NGX = os.environ["NGX"]
sys.path.insert(0, REPO); sys.path.insert(0, os.path.join(REPO, "tests"))
import clock_usage_relay as relay
import test_relay as tr

logbuf = io.StringIO()
logging.basicConfig(level=logging.INFO, stream=logbuf, format="%(message)s")
mock = tr.MockUpstream()
creds = os.path.join(T, "credentials.json")
json.dump({"claudeAiOauth": {"accessToken": tr.OLD_ACCESS, "refreshToken": tr.OLD_REFRESH,
           "expiresAt": int((time.time() + 3600) * 1000), "scopes": ["user:profile"]}}, open(creds, "w"))
os.chmod(creds, 0o600)
cfg = relay.Config({"RELAY_BIND": "127.0.0.2", "RELAY_PORT": "18787", "RELAY_TRUSTED_PROXIES": "127.0.0.3",
                    "RELAY_USAGE_URL": mock.url + "/api/oauth/usage", "RELAY_TOKEN_URL": mock.url + "/v1/oauth/token",
                    "RELAY_CREDENTIALS_FILE": creds, "RELAY_CLOCK_TOKEN": tr.CLOCK_TOKEN,
                    "RELAY_SAMPLES_DB": os.path.join(T, "samples.db")})
server, sampler = relay.build_server(cfg)
sampler.tick()
sampler.start()
threading.Thread(target=server.serve_forever, daemon=True).start()

def nginx(*a):
    subprocess.run([NGX, "-c", T + "/nginx.conf", "-p", T, *a], check=True, capture_output=True)
nginx()
time.sleep(0.5)
ctx = ssl.create_default_context(cafile=T + "/ca.pem")
ctx.minimum_version = ctx.maximum_version = ssl.TLSVersion.TLSv1_2
ctx.set_ciphers("ECDHE-ECDSA-AES128-GCM-SHA256")
import http.client
import socket


def req(path="/clock/usage", token=tr.CLOCK_TOKEN, method="GET", headers=None, direct=False):
    tls = None
    if direct:
        c = http.client.HTTPConnection("127.0.0.2", 18787, timeout=10)
    else:
        c = http.client.HTTPSConnection("127.0.0.1", 18443, context=ctx, timeout=10)
        c.sock = ctx.wrap_socket(socket.create_connection(("127.0.0.1", 18443)), server_hostname="clock.test")
        tls = (c.sock.version(), c.sock.cipher()[0])
    h = {"Host": "clock.test"}
    if token:
        h["Authorization"] = "Bearer " + token
    h.update(headers or {})
    c.request(method, path, headers=h)
    r = c.getresponse()
    body = r.read().decode(errors="replace")
    c.close()
    return r.status, body, tls


results = []
def check(name, cond, detail=""):
    results.append((name, bool(cond))); print(("PASS " if cond else "FAIL ") + name, detail)

s, b, tls = req(); check("TLS1.2 ECDHE-ECDSA-AES128-GCM handshake + 200 with token", s == 200 and json.loads(b)["status"] == "ok", (tls, b[:80]))
s, b, _ = req(token=None); check("no token -> 401 (relay)", s == 401 and "unauthorized" in b)
s, b, _ = req(path="/healthz"); check("/healthz -> 404 (nginx)", s == 404)
s, b, _ = req(path="/"); check("/ -> 404", s == 404)
s, b, _ = req(method="POST"); check("POST -> 403 (limit_except)", s == 403)
codes = [req()[0] for _ in range(6)]
s, b, _ = req(); check("nginx limit_req: burst exceeded -> 429 html", s == 429 and "<html>" in b, codes)

# phase B: drop nginx rate limit, test relay lockout through the proxy
conf = open(T + "/site.conf").read()
open(T + "/site.conf", "w").write("\n".join(l for l in conf.splitlines() if "limit_req zone" not in l and "limit_req_status" not in l))
nginx("-s", "reload"); time.sleep(0.8)
s, b, _ = req(path="/clock/usage?hourly=1")
check("?hourly=1 passes through nginx (24 buckets per item)", s == 200 and all(len(i["hourly"]) == 24 for i in json.loads(b)["items"]), b[:60])
for i in range(10):
    req(token="wrong-token-wrong-token", headers={"X-Forwarded-For": "198.51.100.%d" % i})
s, b, _ = req(headers={"X-Forwarded-For": "203.0.113.9"})
check("spoofed XFF ignored: real client locked out after 10 bad tokens -> relay 429", s == 429 and "too_many_failures" in b, b)
logs = logbuf.getvalue()
check("relay saw real client 127.0.0.1 (not proxy 127.0.0.3, not spoof)", "client 127.0.0.1 locked out" in logs and "198.51.100" not in logs and "127.0.0.3" not in logs.split("locked out")[0][-40:])
s, b, _ = req(direct=True, headers={"X-Forwarded-For": "203.0.113.50"})
check("direct (untrusted) peer 127.0.0.1 cannot escape lockout with forged XFF -> 429", s == 429 and "too_many_failures" in b, s)
access = open(T + "/logs/clock-usage-relay.access.log").read()
check("token absent from nginx access log and relay log", tr.CLOCK_TOKEN not in access and tr.CLOCK_TOKEN not in logs and "sk-ant" not in logs)
check("upstream called once (sampler, not per clock request)", mock.usage_calls == 1, mock.usage_calls)
nginx("-s", "stop"); server.shutdown(); sampler.stop(); mock.close()
print("%d/%d passed" % (sum(ok for _, ok in results), len(results)))
sys.exit(0 if all(ok for _, ok in results) else 1)
