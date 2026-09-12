#!/usr/bin/env bash
set -euo pipefail

binary="${1:-./build/onuros_stage7_tls_probe}"
work="$(mktemp -d)"
server_pid=""
cleanup() {
  if [[ -n "$server_pid" ]]; then kill "$server_pid" 2>/dev/null || true; fi
  rm -rf "$work"
}
trap cleanup EXIT

bash "$(dirname "$0")/generate-stage7-tls-material.sh" \
  "$work/tls" onuros-server onuros-client >/dev/null
port=39443

"$binary" server 127.0.0.1 "$port" \
  "$work/tls/server.crt" "$work/tls/server.key" "$work/tls/ca.crt" \
  onuros-client >"$work/server.log" 2>&1 &
server_pid=$!
for _ in {1..200}; do
  grep -q '^READY ' "$work/server.log" && break
  kill -0 "$server_pid" 2>/dev/null || {
    cat "$work/server.log" >&2
    exit 1
  }
  sleep 0.01
done
grep -q '^READY ' "$work/server.log"

"$binary" client 127.0.0.1 "$port" \
  "$work/tls/client.crt" "$work/tls/client.key" "$work/tls/ca.crt" \
  onuros-server stage7-ci
wait "$server_pid"
server_pid=""
cat "$work/server.log"
grep -q '^tls_server=PASS ' "$work/server.log"
printf 'independent_tls_loopback=PASS peer_names=verified\n'
