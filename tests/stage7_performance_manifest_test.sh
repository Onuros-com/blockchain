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
queue_high_watermark=32
queue_limit=32
verification_batches=2750
verification_tasks=66000
verification_workers=8
verification_pool_starts=1
verification_seconds=550.000000
admitted_tps=110.000000
divergent_transactions=0
limits_exceeded=0
payment_bytes=584
proof_system=groth16-bls12-381
commitment_hash=poseidon
verification_backend=onuros-privacy-engine-abi-v1
tracked_witness_backend=onuros-privacy-engine-abi-v2
qualification_profile=groth16-poseidon-payment-relay-v1
active_privacy_protocol_qualified=true
genesis_sync_qualified=false
network_id=1
circuit_version=1
root_height=100
genesis=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
candidate_root=dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd
parameters_sha256=eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee
corpus_sha256=abababababababababababababababababababababababababababababababab
initial_commitments_sha256=cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd
tls_ca_sha256=ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
blockchain_commit=1111111111111111111111111111111111111111
privacy_lab_commit=2222222222222222222222222222222222222222
node_id=$node
loopback=false
transport_authenticated=true
process_exit_status=0
private_payloads_logged=false
id_set_sha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
EOF
  role_index=$((role_index + 1))
done
bash "$validator" relay "$work/a.relay" "$work/b.relay" "$work/c.relay" |
  grep -q '^stage7_unique_relay_gate=PASS '

sed -i 's/^queue_high_watermark=32$/queue_high_watermark=0/' "$work/c.relay"
if bash "$validator" relay "$work/a.relay" "$work/b.relay" \
    "$work/c.relay" >/dev/null 2>&1; then
  echo "validator accepted an uninstrumented queue" >&2
  exit 1
fi
sed -i 's/^queue_high_watermark=0$/queue_high_watermark=32/' "$work/c.relay"

sed -i 's/^verification_pool_starts=1$/verification_pool_starts=2/' \
  "$work/c.relay"
if bash "$validator" relay "$work/a.relay" "$work/b.relay" \
    "$work/c.relay" >/dev/null 2>&1; then
  echo "validator accepted restarted verification workers" >&2
  exit 1
fi
sed -i 's/^verification_pool_starts=2$/verification_pool_starts=1/' \
  "$work/c.relay"

sed -i 's/^admitted_tps=110.000000$/admitted_tps=109.000000/' \
  "$work/c.relay"
if bash "$validator" relay "$work/a.relay" "$work/b.relay" \
    "$work/c.relay" >/dev/null 2>&1; then
  echo "validator accepted inconsistent reported throughput" >&2
  exit 1
fi
sed -i 's/^admitted_tps=109.000000$/admitted_tps=110.000000/' \
  "$work/c.relay"

sed -i 's/^role=observer$/role=relay/' "$work/c.relay"
if bash "$validator" relay "$work/a.relay" "$work/b.relay" \
    "$work/c.relay" >/dev/null 2>&1; then
  echo "validator accepted repeated relay role" >&2
  exit 1
fi
sed -i 's/^role=relay$/role=observer/' "$work/c.relay"

for receiver in a b; do
  cat >"$work/$receiver.block" <<EOF
role=receiver
receiver_id=$receiver
node_id=$receiver
block_bytes=3552300
transactions=6001
mempool_overlap_percent=50
mempool_overlap_transactions=3000
announcement_bytes=58168
request_bytes=3728
response_bytes=8300000
propagation_validation_seconds=29.999
block_id=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
limits_exceeded=0
validation=PASS
durable_activation=PASS
late_catch_up=PASS
restart_recovery=PASS
offline_restart=PENDING_SEPARATE_PROCESS
payment_bytes=584
proof_system=groth16-bls12-381
commitment_hash=poseidon
tracked_witness_backend=onuros-privacy-engine-abi-v2
qualification_profile=groth16-poseidon-genesis-sync-v1
active_privacy_protocol_qualified=true
genesis_sync_qualified=true
loopback=false
transport_authenticated=true
genesis=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
candidate_root=dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd
terminal_note_root=9999999999999999999999999999999999999999999999999999999999999999
parameters_sha256=eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee
corpus_sha256=abababababababababababababababababababababababababababababababab
initial_commitments_sha256=cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd
tls_ca_sha256=ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
blockchain_commit=1111111111111111111111111111111111111111
privacy_lab_commit=2222222222222222222222222222222222222222
network_id=1
circuit_version=1
root_height=100
verification_backend=onuros-privacy-engine-abi-v1
process_exit_status=0
private_payloads_logged=false
EOF
done
for receiver in a b; do
  cat >"$work/$receiver.restart" <<EOF
role=restart
node_id=$receiver
offline_restart=PASS
network_attempted=false
process_exit_status=0
tip=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
genesis=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
candidate_root=dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd
terminal_note_root=9999999999999999999999999999999999999999999999999999999999999999
parameters_sha256=eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee
corpus_sha256=abababababababababababababababababababababababababababababababab
initial_commitments_sha256=cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd
tls_ca_sha256=ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
blockchain_commit=1111111111111111111111111111111111111111
privacy_lab_commit=2222222222222222222222222222222222222222
network_id=1
circuit_version=1
root_height=100
EOF
done
bash "$validator" block "$work/a.block" "$work/b.block" \
  "$work/a.restart" "$work/b.restart" |
  grep -q '^stage7_candidate_sync_gate=PASS '

sed -i 's/^receiver_id=b$/receiver_id=a/' "$work/b.block"
if bash "$validator" block "$work/a.block" "$work/b.block" \
    "$work/a.restart" "$work/b.restart" \
    >/dev/null 2>&1; then
  echo "validator accepted repeated block receiver" >&2
  exit 1
fi
sed -i 's/^receiver_id=a$/receiver_id=b/' "$work/b.block"

sed -i 's/29.999/30.001/' "$work/b.block"
if bash "$validator" block "$work/a.block" "$work/b.block" \
    "$work/a.restart" "$work/b.restart" >/dev/null 2>&1; then
  echo "validator accepted excess block latency" >&2
  exit 1
fi
printf 'stage7_performance_manifest_validator=PASS\n'
