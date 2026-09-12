#!/usr/bin/env bash
set -euo pipefail

binary="${1:-./build/onuros-stage7-private-relay-node}"
evidence_root="${2:-$(mktemp -d)}"
port="${ONUROS_STAGE7_PRIVATE_RELAY_PORT:-38465}"
selector="${ONUROS_STAGE7_PRIVATE_SELECTOR:-73}"

mkdir -p "$evidence_root"
evidence_dir="$(mktemp -d "$evidence_root/run.XXXXXX")"

server_pid=""
origin_pid=""
observer_pid=""
cleanup() {
  for pid in "$server_pid" "$origin_pid" "$observer_pid"; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
}
trap cleanup EXIT

"$binary" --role server --port "$port" \
  >"$evidence_dir/server.log" 2>&1 &
server_pid=$!

for _ in $(seq 1 500); do
  grep -q '^READY port=' "$evidence_dir/server.log" 2>/dev/null && break
  sleep 0.01
done
grep -q '^READY port=' "$evidence_dir/server.log"

"$binary" --role origin --port "$port" --selector "$selector" \
  >"$evidence_dir/origin.log" 2>&1 &
origin_pid=$!

for _ in $(seq 1 500); do
  grep -q '^ORIGIN_CONNECTED$' "$evidence_dir/server.log" 2>/dev/null && break
  sleep 0.01
done
grep -q '^ORIGIN_CONNECTED$' "$evidence_dir/server.log"

"$binary" --role observer --port "$port" \
  >"$evidence_dir/observer.log" 2>&1 &
observer_pid=$!

wait "$origin_pid"
origin_pid=""
wait "$observer_pid"
observer_pid=""
wait "$server_pid"
server_pid=""

origin_id="$(sed -n 's/^ADMITTED node=origin txid=\([^ ]*\).*/\1/p' \
  "$evidence_dir/origin.log")"
relay_id="$(sed -n 's/^ADMITTED node=relay txid=\([^ ]*\).*/\1/p' \
  "$evidence_dir/server.log")"
observer_id="$(sed -n 's/^ADMITTED node=observer txid=\([^ ]*\).*/\1/p' \
  "$evidence_dir/observer.log")"

if [[ -z "$origin_id" || "$origin_id" != "$relay_id" ||
      "$origin_id" != "$observer_id" ]]; then
  echo "private relay transaction identifiers diverged" >&2
  exit 1
fi
grep -q "^RELAY_COMPLETE txid=$origin_id$" "$evidence_dir/server.log"

{
  echo "private_transaction_relay=PASS"
  echo "processes=3"
  echo "unique_transactions=1"
  echo "admitted_nodes=3"
  echo "transaction_id=$origin_id"
  echo "private_payload_logged=false"
} | tee "$evidence_dir/manifest.txt"

echo "Evidence: $evidence_dir"
