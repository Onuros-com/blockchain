#!/usr/bin/env bash
set -euo pipefail

if (( $# != 3 )); then
  echo "usage: $0 NODE CORPUS_GENERATOR OUTPUT" >&2
  exit 2
fi

node="$1"
generator="$2"
output="$3"
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
port="${ONUROS_STAGE7_LOAD_TEST_PORT:-$((41000 + RANDOM % 8000))}"
work="$(mktemp -d)"
relay_pid=""
origin_pid=""
observer_pid=""
cleanup() {
  for pid in "$relay_pid" "$origin_pid" "$observer_pid"; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  rm -rf -- "$work"
}
trap cleanup EXIT INT TERM
mkdir -p "$output"

bash "$repo_dir/scripts/generate-stage7-tls-material.sh" \
  "$work/tls" load-relay load-client >/dev/null
"$generator" --generate-corpus-shards "$work/corpus.onc" 4 2 0 1 \
  >"$output/generator.log" 2>&1
"$generator" --generate-corpus-shards "$work/corpus.onc" 4 2 0 1 \
  >>"$output/generator.log" 2>&1
grep -q '^corpus_shard=REUSED shard=0 ' "$output/generator.log"
"$generator" --generate-corpus-shards "$work/corpus.onc" 4 2 1 2 \
  >>"$output/generator.log" 2>&1
"$generator" --merge-corpus-shards "$work/corpus.onc" 4 2 \
  >>"$output/generator.log" 2>&1
grep -q '^orchard_corpus=PASS transactions=4 shards=2 unique=4 ' \
  "$output/generator.log"

wait_for_log() {
  local pattern="$1" log="$2" pid="$3"
  for _ in $(seq 1 3000); do
    grep -q "$pattern" "$log" 2>/dev/null && return 0
    if ! kill -0 "$pid" 2>/dev/null; then cat "$log" >&2; return 1; fi
    sleep 0.01
  done
  cat "$log" >&2
  return 1
}

"$node" --role relay --bind 127.0.0.1 --port "$port" \
  --cert "$work/tls/server.crt" --key "$work/tls/server.key" \
  --ca "$work/tls/ca.crt" --expected-origin load-client \
  --expected-observer load-client --workers 2 \
  --manifest "$output/relay.manifest" >"$output/relay.log" 2>&1 &
relay_pid=$!
wait_for_log '^READY role=relay ' "$output/relay.log" "$relay_pid"

"$node" --role origin --address 127.0.0.1 --port "$port" \
  --cert "$work/tls/client.crt" --key "$work/tls/client.key" \
  --ca "$work/tls/ca.crt" --expected-relay load-relay \
  --corpus "$work/corpus.onc" --duration 1 --rate 4 --batch 2 --workers 2 \
  --manifest "$output/origin.manifest" >"$output/origin.log" 2>&1 &
origin_pid=$!
wait_for_log '^CONNECTED peer=origin$' "$output/relay.log" "$relay_pid"

"$node" --role observer --address 127.0.0.1 --port "$port" \
  --cert "$work/tls/client.crt" --key "$work/tls/client.key" \
  --ca "$work/tls/ca.crt" --expected-relay load-relay --workers 2 \
  --manifest "$output/observer.manifest" >"$output/observer.log" 2>&1 &
observer_pid=$!

wait "$origin_pid"; origin_pid=""
wait "$observer_pid"; observer_pid=""
wait "$relay_pid"; relay_pid=""

for role in origin relay observer; do
  grep -q "^stage7_private_load=PASS role=$role " "$output/$role.log"
done
origin_hash="$(awk -F= '$1=="id_set_sha256" {print $2}' "$output/origin.manifest")"
relay_hash="$(awk -F= '$1=="id_set_sha256" {print $2}' "$output/relay.manifest")"
observer_hash="$(awk -F= '$1=="id_set_sha256" {print $2}' "$output/observer.manifest")"
[[ -n "$origin_hash" && "$origin_hash" == "$relay_hash" &&
   "$origin_hash" == "$observer_hash" ]]
printf 'stage7_private_load_loopback=PASS transactions=4 id_set_sha256=%s\n' \
  "$origin_hash"
