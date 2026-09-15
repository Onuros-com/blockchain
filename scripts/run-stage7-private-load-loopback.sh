#!/usr/bin/env bash
set -euo pipefail

if (( $# != 2 )); then
  echo "usage: $0 NODE OUTPUT" >&2
  exit 2
fi

node="$1"
output="$2"
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
corpus_dir="${ONUROS_CORPUS_DIR:?set ONUROS_CORPUS_DIR}"
parameters_sha256="${ONUROS_EXPECTED_PARAMS_SHA256:?set ONUROS_EXPECTED_PARAMS_SHA256}"
candidate_root="${ONUROS_CANDIDATE_ROOT:?set ONUROS_CANDIDATE_ROOT}"
network_id="${ONUROS_NETWORK_ID:?set ONUROS_NETWORK_ID}"
circuit_version="${ONUROS_CIRCUIT_VERSION:?set ONUROS_CIRCUIT_VERSION}"
root_height="${ONUROS_ROOT_HEIGHT:?set ONUROS_ROOT_HEIGHT}"
blockchain_commit="${ONUROS_BLOCKCHAIN_COMMIT:?set ONUROS_BLOCKCHAIN_COMMIT}"
privacy_lab_commit="${ONUROS_PRIVACY_LAB_COMMIT:?set ONUROS_PRIVACY_LAB_COMMIT}"
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
[[ -f "$corpus_dir/payments.bin" && -f "$corpus_dir/params.bin" ]] || {
  echo "candidate corpus or parameters missing" >&2
  exit 1
}

common=(--parameters "$corpus_dir/params.bin"
  --parameters-sha256 "$parameters_sha256"
  --network-id "$network_id" --circuit-version "$circuit_version"
  --root-height "$root_height" --root "$candidate_root"
  --blockchain-commit "$blockchain_commit"
  --privacy-lab-commit "$privacy_lab_commit")

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
  --node-id loopback-relay "${common[@]}" \
  --manifest "$output/relay.manifest" >"$output/relay.log" 2>&1 &
relay_pid=$!
wait_for_log '^READY role=relay ' "$output/relay.log" "$relay_pid"

"$node" --role origin --address 127.0.0.1 --port "$port" \
  --cert "$work/tls/client.crt" --key "$work/tls/client.key" \
  --ca "$work/tls/ca.crt" --expected-relay load-relay \
  --corpus "$corpus_dir/payments.bin" --duration 1 --rate 4 --batch 2 --workers 2 \
  --node-id loopback-origin "${common[@]}" \
  --manifest "$output/origin.manifest" >"$output/origin.log" 2>&1 &
origin_pid=$!
wait_for_log '^CONNECTED peer=origin$' "$output/relay.log" "$relay_pid"

"$node" --role observer --address 127.0.0.1 --port "$port" \
  --cert "$work/tls/client.crt" --key "$work/tls/client.key" \
  --ca "$work/tls/ca.crt" --expected-relay load-relay --workers 2 \
  --node-id loopback-observer "${common[@]}" \
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
printf 'stage7_private_load_loopback=PASS qualification=false transactions=4 id_set_sha256=%s\n' \
  "$origin_hash"
