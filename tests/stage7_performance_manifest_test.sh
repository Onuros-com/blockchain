#!/usr/bin/env bash
set -euo pipefail

validator="${1:?validator path required}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

role_index=0
for node in a b c; do
  role="$(printf '%s\n' origin relay observer | sed -n "$((role_index + 1))p")"
  cat >"$work/$node.relay" <<EOF
role=$role
duration_seconds=600.000
submitted_unique=66000
admitted_unique=66000
relayed_unique=66000
duplicate_transactions=0
invalid_transactions=0
queue_high_watermark=0
divergent_transactions=0
limits_exceeded=0
verification_backend=orchard-ffi
process_exit_status=0
private_payloads_logged=false
id_set_sha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
EOF
  role_index=$((role_index + 1))
done
bash "$validator" relay "$work/a.relay" "$work/b.relay" "$work/c.relay" |
  grep -q '^stage7_unique_relay_gate=PASS '

sed -i 's/^role=observer$/role=relay/' "$work/c.relay"
if bash "$validator" relay "$work/a.relay" "$work/b.relay" \
    "$work/c.relay" >/dev/null 2>&1; then
  echo "validator accepted repeated relay role" >&2
  exit 1
fi
sed -i 's/^role=relay$/role=observer/' "$work/c.relay"

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
