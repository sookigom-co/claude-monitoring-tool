#!/usr/bin/env bash
# Runs relay behind a real nginx using deploy/nginx/clock-usage-relay.conf; usage: run_nginx_it.sh [path/to/nginx]
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
NGX="${1:-$(command -v nginx)}"
T="$(mktemp -d)"
trap '"$NGX" -c "$T/nginx.conf" -p "$T" -s stop 2>/dev/null || true; rm -rf "$T"' EXIT
mkdir -p "$T/logs"
cd "$T"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout ca.key -out ca.pem -days 2 -subj "/CN=Test Root" \
  -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null
openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout leaf.key -out leaf.csr -subj "/CN=clock.test" 2>/dev/null
printf 'subjectAltName=DNS:clock.test\nauthorityKeyIdentifier=keyid\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=serverAuth\n' > ext.cnf
openssl x509 -req -in leaf.csr -CA ca.pem -CAkey ca.key -CAcreateserial -out leaf.pem -days 2 -extfile ext.cnf 2>/dev/null
cat leaf.pem ca.pem > fullchain.pem
sed -e "s#clock.example.com#clock.test#; s#/etc/letsencrypt/live/clock.test/fullchain.pem#$T/fullchain.pem#; s#/etc/letsencrypt/live/clock.test/privkey.pem#$T/leaf.key#" \
    -e "s#RELAY_HOST:8787#127.0.0.2:18787#; s#listen 443 ssl;#listen 127.0.0.1:18443 ssl;#; /listen \[::\]:443/d; s#/var/log/nginx#$T/logs#" \
    -e "s#proxy_pass http://clock_usage_relay;#proxy_pass http://clock_usage_relay;\n        proxy_bind 127.0.0.3;#" \
    "$REPO/deploy/nginx/clock-usage-relay.conf" > site.conf
cat > nginx.conf <<CONF
worker_processes 1;
error_log $T/logs/error.log warn;
pid $T/nginx.pid;
events { worker_connections 64; }
http {
  client_body_temp_path $T/tmp_body; proxy_temp_path $T/tmp_proxy; fastcgi_temp_path $T/tmp_fcgi;
  uwsgi_temp_path $T/tmp_uwsgi; scgi_temp_path $T/tmp_scgi;
  include $T/site.conf;
}
CONF
T="$T" REPO="$REPO" NGX="$NGX" python3 "$REPO/tests/integration/nginx_it.py"
