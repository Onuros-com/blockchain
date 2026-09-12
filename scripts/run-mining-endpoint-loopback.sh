#!/usr/bin/env bash
set -euo pipefail

readonly binary="${1:-./build/onuros-mining-endpoint}"
readonly evidence_root="${2:-$(mktemp -d)}"
# WSL2 virtioProxy can retain a closed fixed-port mapping briefly, while some
# versions also refuse listeners created with port zero. Select a fresh bounded
# fixed port for each run and retain the environment override for reproducible
# diagnostics.
readonly base_port="${ONUROS_MINING_TEST_PORT:-$((40000 + RANDOM % 8000))}"

mkdir -p "$evidence_root"
readonly evidence_dir="$(mktemp -d "$evidence_root/run.XXXXXX")"
server_pid=''

cleanup() {
  if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
    kill "$server_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT

run_case() {
  local name="$1"
  local port="$2"
  local expected="$3"
  shift 3
  "$binary" --role server --data "$evidence_dir/$name.db" --port "$port" \
    >"$evidence_dir/$name-server.log" 2>&1 &
  server_pid=$!
  "$binary" --role client --port "$port" "$@" \
    >"$evidence_dir/$name-client.log" 2>&1
  wait "$server_pid"
  server_pid=''
  grep -q "^${expected} job=" "$evidence_dir/$name-server.log"
  grep -q "^${expected} job=" "$evidence_dir/$name-client.log"
}

run_case valid "$base_port" ACCEPTED
run_case altered "$((base_port + 1))" REJECTED --alter-mix
"$binary" --role server --data "$evidence_dir/probed.db" \
  --port "$((base_port + 2))" --submissions 2 \
  >"$evidence_dir/probed-server.log" 2>&1 &
server_pid=$!
"$binary" --role client --port "$((base_port + 2))" --negative-probe \
  >"$evidence_dir/probed-client.log" 2>&1
wait "$server_pid"
server_pid=''
grep -q '^REJECTED job=.*submission=1 result_code=2' \
  "$evidence_dir/probed-server.log"
grep -q '^ACCEPTED job=.*submission=2 result_code=0' \
  "$evidence_dir/probed-server.log"
grep -q '^EXPECTED_REJECTION job=' "$evidence_dir/probed-client.log"
grep -q '^ACCEPTED job=' "$evidence_dir/probed-client.log"

echo "Stage 7 KawPoW mining endpoint loopback passed"
echo "Evidence: $evidence_dir"
