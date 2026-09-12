#!/usr/bin/env bash
set -euo pipefail

binary="${1:-./build/onuros-stage7-network-node}"
evidence_root="${2:-$(mktemp -d)}"
port="${ONUROS_STAGE7_TEST_PORT:-38455}"

mkdir -p "$evidence_root"
evidence_dir="$(mktemp -d "$evidence_root/run.XXXXXX")"
server_db="$evidence_dir/server.db"
node_b_db="$evidence_dir/node-b.db"
node_c_db="$evidence_dir/node-c.db"

"$binary" --role server --data "$server_db" --port "$port" --peers 2 --blocks 3 \
  >"$evidence_dir/server.log" 2>&1 &
server_pid=$!

cleanup() {
  if kill -0 "$server_pid" 2>/dev/null; then
    kill "$server_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT

"$binary" --role client --data "$node_b_db" --port "$port" --nonce 20001 --blocks 3 \
  >"$evidence_dir/node-b.log" 2>&1 &
node_b_pid=$!
"$binary" --role client --data "$node_c_db" --port "$port" --nonce 20002 --blocks 3 \
  >"$evidence_dir/node-c.log" 2>&1 &
node_c_pid=$!

wait "$node_b_pid"
wait "$node_c_pid"
wait "$server_pid"

server_tip="$(sed -n 's/.* tip=//p' "$evidence_dir/server.log" | tail -1)"
node_b_tip="$(sed -n 's/.* tip=//p' "$evidence_dir/node-b.log" | tail -1)"
node_c_tip="$(sed -n 's/.* tip=//p' "$evidence_dir/node-c.log" | tail -1)"

if [[ -z "$server_tip" || "$server_tip" != "$node_b_tip" ||
      "$server_tip" != "$node_c_tip" ]]; then
  echo "three-process convergence failed" >&2
  exit 1
fi

"$binary" --role client --data "$node_b_db" --port "$port" --nonce 20003 --blocks 3 \
  >"$evidence_dir/node-b-restart.log" 2>&1
if ! grep -q '^RECOVERED height=3 tip=' "$evidence_dir/node-b-restart.log"; then
  echo "restart recovery failed" >&2
  exit 1
fi

echo "Stage 7 three-process convergence passed"
echo "Tip: $server_tip"
echo "Evidence: $evidence_dir"
