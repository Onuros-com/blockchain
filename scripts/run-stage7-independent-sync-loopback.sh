#!/usr/bin/env bash
set -euo pipefail

binary="${1:?usage: run-stage7-independent-sync-loopback.sh BINARY OUTPUT}"
output="${2:?missing output directory}"
port="${ONUROS_STAGE7_SYNC_PORT:-39447}"

mkdir -p "$output"
run_dir="$(mktemp -d "$output/run.XXXXXX")"
tls_dir="$run_dir/tls"

bash "$(dirname "$0")/generate-stage7-tls-material.sh" \
  "$tls_dir" stage7-sync-server stage7-sync-client >/dev/null

server_log="$run_dir/server.log"
"$binary" \
  --role server --data "$run_dir/server.db" --port "$port" \
  --bind 127.0.0.1 --peers 2 --blocks 3 --node-id sync-server \
  --manifest "$run_dir/server.manifest" \
  --cert "$tls_dir/server.crt" --key "$tls_dir/server.key" \
  --ca "$tls_dir/ca.crt" \
  --expected-peer stage7-sync-client \
  --expected-peer stage7-sync-client \
  >"$server_log" 2>&1 &
server_pid=$!

cleanup() {
  if kill -0 "$server_pid" 2>/dev/null; then
    kill "$server_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT

for _ in $(seq 1 100); do
  grep -q '^READY ' "$server_log" 2>/dev/null && break
  kill -0 "$server_pid" 2>/dev/null || {
    cat "$server_log" >&2
    exit 1
  }
  sleep 0.05
done
grep -q '^READY ' "$server_log"

"$binary" \
  --role client --data "$run_dir/client-a.db" --port "$port" \
  --address 127.0.0.1 --blocks 3 --nonce 20001 \
  --node-id sync-client-a --manifest "$run_dir/client-a.manifest" \
  --cert "$tls_dir/client.crt" --key "$tls_dir/client.key" \
  --ca "$tls_dir/ca.crt" --expected-peer stage7-sync-server \
  >"$run_dir/client-a.log" 2>&1 &
client_a_pid=$!

"$binary" \
  --role client --data "$run_dir/client-b.db" --port "$port" \
  --address 127.0.0.1 --blocks 3 --nonce 20002 \
  --node-id sync-client-b --manifest "$run_dir/client-b.manifest" \
  --cert "$tls_dir/client.crt" --key "$tls_dir/client.key" \
  --ca "$tls_dir/ca.crt" --expected-peer stage7-sync-server \
  >"$run_dir/client-b.log" 2>&1 &
client_b_pid=$!

wait "$client_a_pid"
wait "$client_b_pid"
wait "$server_pid"

"$binary" \
  --role client --data "$run_dir/client-a.db" --port "$port" \
  --address 127.0.0.1 --blocks 3 --nonce 20003 \
  --node-id sync-client-a --manifest "$run_dir/client-a-restart.manifest" \
  >"$run_dir/client-a-restart.log" 2>&1

server_tip="$(sed -n 's/^tip=//p' "$run_dir/server.manifest")"
client_a_tip="$(sed -n 's/^tip=//p' "$run_dir/client-a.manifest")"
client_b_tip="$(sed -n 's/^tip=//p' "$run_dir/client-b.manifest")"
restart_tip="$(sed -n 's/^tip=//p' "$run_dir/client-a-restart.manifest")"

[[ -n "$server_tip" && "$server_tip" == "$client_a_tip" && \
   "$server_tip" == "$client_b_tip" && "$server_tip" == "$restart_tip" ]]
grep -q '^transport_authenticated=true$' "$run_dir/server.manifest"
grep -q '^transport_authenticated=true$' "$run_dir/client-a.manifest"
grep -q '^transport_authenticated=true$' "$run_dir/client-b.manifest"
grep -q '^mode=recovered$' "$run_dir/client-a-restart.manifest"
grep -q '^synchronization=PASS$' "$run_dir/client-a-restart.manifest"

if "$binary" --role client --data "$run_dir/reject.db" --port "$port" \
    --address 192.0.2.1 --blocks 3 >/dev/null 2>&1; then
  echo "non-TLS non-loopback client was accepted" >&2
  exit 1
fi

bash "$(dirname "$0")/capture-stage7-sync-evidence.sh" \
  "$run_dir/final-server" server "$run_dir/server.manifest" \
  "$run_dir/server.log" "$binary" "$tls_dir/server.crt" \
  "$tls_dir/ca.crt" >/dev/null
bash "$(dirname "$0")/capture-stage7-sync-evidence.sh" \
  "$run_dir/final-client-a" client "$run_dir/client-a.manifest" \
  "$run_dir/client-a.log" "$binary" "$tls_dir/client.crt" \
  "$tls_dir/ca.crt" >/dev/null
bash "$(dirname "$0")/capture-stage7-sync-evidence.sh" \
  "$run_dir/final-client-b" client "$run_dir/client-b.manifest" \
  "$run_dir/client-b.log" "$binary" "$tls_dir/client.crt" \
  "$tls_dir/ca.crt" >/dev/null
bash "$(dirname "$0")/capture-stage7-sync-evidence.sh" \
  "$run_dir/final-client-a-restart" client \
  "$run_dir/client-a-restart.manifest" "$run_dir/client-a-restart.log" \
  "$binary" "$tls_dir/client.crt" "$tls_dir/ca.crt" >/dev/null

bash "$(dirname "$0")/validate-stage7-sync-evidence.sh" \
  "$run_dir/final-server" "$run_dir/final-client-a" \
  "$run_dir/final-client-b" "$run_dir/final-client-a-restart" >/dev/null

cp -a "$run_dir/final-client-b" "$run_dir/tampered-client-b"
sed -i 's/^node_id=sync-client-b$/node_id=sync-client-a/' \
  "$run_dir/tampered-client-b/node-manifest.txt"
tampered_hash="$(sha256sum "$run_dir/tampered-client-b/node-manifest.txt" | \
  awk '{print $1}')"
sed -i "s/^node_manifest_sha256=.*/node_manifest_sha256=$tampered_hash/" \
  "$run_dir/tampered-client-b/host-manifest.txt"
if bash "$(dirname "$0")/validate-stage7-sync-evidence.sh" \
    "$run_dir/final-server" "$run_dir/final-client-a" \
    "$run_dir/tampered-client-b" \
    "$run_dir/final-client-a-restart" >/dev/null 2>&1; then
  echo "duplicate physical node identity was accepted" >&2
  exit 1
fi
rm -rf "$run_dir/tampered-client-b"

find "$run_dir" -type f \( -name '*.key' -o -name 'ca.srl' \) -delete
echo "stage7_independent_sync_loopback=PASS tip=$server_tip"
echo "evidence=$run_dir"
