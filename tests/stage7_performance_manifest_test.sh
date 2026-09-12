#!/usr/bin/env bash
set -euo pipefail

validator="${1:?validator path required}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

for node in a b c; do
  cat >"$work/$node.relay" <<EOF
duration_seconds=600
admitted_unique=60000
relayed_unique=60000
divergent_transactions=0
limits_exceeded=0
verification_backend=orchard-ffi
process_exit_status=0
private_payloads_logged=false
id_set_sha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
EOF
done
bash "$validator" relay "$work/a.relay" "$work/b.relay" "$work/c.relay" |
  grep -q '^stage7_unique_relay_gate=PASS '

for receiver in a b; do
  cat >"$work/$receiver.block" <<EOF
block_bytes=16612467
propagation_validation_seconds=29.999
block_id=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
limits_exceeded=0
validation=PASS
durable_activation=PASS
restart_recovery=PASS
EOF
done
bash "$validator" block "$work/a.block" "$work/b.block" |
  grep -q '^stage7_block_propagation_gate=PASS '

sed -i 's/29.999/30.001/' "$work/b.block"
if bash "$validator" block "$work/a.block" "$work/b.block" >/dev/null 2>&1; then
  echo "validator accepted excess block latency" >&2
  exit 1
fi
printf 'stage7_performance_manifest_validator=PASS\n'
