#!/usr/bin/env bash
set -euo pipefail

bundle_validator="${1:?bundle validator path required}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
commit="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
set_hash="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
block_id="cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
binary_hash="dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
certificate_hash="eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
ca_hash="ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"

write_host_manifest() {
  local directory="$1" pass_line="$2" selected_commit="${3:-$commit}"
  local manifest_hash log_hash
  manifest_hash="$(sha256sum "$directory/node-manifest.txt" | awk '{print $1}')"
  log_hash="$(sha256sum "$directory/node.log" | awk '{print $1}')"
  cat >"$directory/host-manifest.txt" <<EOF
commit=$selected_commit
binary_sha256=$binary_hash
certificate_sha256=$certificate_hash
ca_certificate_sha256=$ca_hash
node_manifest_sha256=$manifest_hash
node_log_sha256=$log_hash
private_keys_included=false
$pass_line
EOF
}

for role in origin relay observer; do
  directory="$work/$role"
  mkdir -p "$directory"
  cat >"$directory/node-manifest.txt" <<EOF
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
tls_ca_sha256=ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
blockchain_commit=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
privacy_lab_commit=2222222222222222222222222222222222222222
node_id=$role
loopback=false
transport_authenticated=true
process_exit_status=0
private_payloads_logged=false
id_set_sha256=$set_hash
EOF
  printf 'stage7_private_load=PASS role=%s admitted_unique=66000\n' "$role" \
    >"$directory/node.log"
  write_host_manifest "$directory" \
    "stage7_private_load_evidence=PASS role=$role"
done

bash "$bundle_validator" relay "$work/origin" "$work/relay" \
  "$work/observer" | grep -q '^stage7_evidence_bundle=PASS mode=relay '

printf 'tampered\n' >>"$work/observer/node.log"
if bash "$bundle_validator" relay "$work/origin" "$work/relay" \
    "$work/observer" >/dev/null 2>&1; then
  echo "bundle validator accepted a modified log" >&2
  exit 1
fi
sed -i '$d' "$work/observer/node.log"
write_host_manifest "$work/observer" \
  'stage7_private_load_evidence=PASS role=observer'

write_host_manifest "$work/observer" \
  'stage7_private_load_evidence=PASS role=observer' \
  'dddddddddddddddddddddddddddddddddddddddd'
if bash "$bundle_validator" relay "$work/origin" "$work/relay" \
    "$work/observer" >/dev/null 2>&1; then
  echo "bundle validator accepted mixed commits" >&2
  exit 1
fi
write_host_manifest "$work/observer" \
  'stage7_private_load_evidence=PASS role=observer'

for receiver in rtx3060 rtx4070; do
  directory="$work/$receiver"
  mkdir -p "$directory"
  cat >"$directory/node-manifest.txt" <<EOF
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
block_id=$block_id
limits_exceeded=0
validation=PASS
durable_activation=PASS
late_catch_up=PASS
restart_recovery=PASS
offline_restart=PENDING_SEPARATE_PROCESS
payment_bytes=584
proof_system=groth16-bls12-381
commitment_hash=poseidon
verification_backend=onuros-privacy-engine-abi-v1
tracked_witness_backend=onuros-privacy-engine-abi-v2
qualification_profile=groth16-poseidon-genesis-sync-v1
active_privacy_protocol_qualified=true
genesis_sync_qualified=true
loopback=false
transport_authenticated=true
genesis=1111111111111111111111111111111111111111111111111111111111111111
candidate_root=2222222222222222222222222222222222222222222222222222222222222222
terminal_note_root=3333333333333333333333333333333333333333333333333333333333333333
parameters_sha256=4444444444444444444444444444444444444444444444444444444444444444
corpus_sha256=5555555555555555555555555555555555555555555555555555555555555555
tls_ca_sha256=ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
blockchain_commit=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
privacy_lab_commit=2222222222222222222222222222222222222222
network_id=1
circuit_version=1
root_height=100
process_exit_status=0
private_payloads_logged=false
EOF
  printf 'stage7_block_receiver=PASS receiver_id=%s\n' "$receiver" \
    >"$directory/node.log"
  write_host_manifest "$directory" 'stage7_block_evidence=PASS'
done

for receiver in rtx3060 rtx4070; do
  directory="$work/$receiver-restart"
  mkdir -p "$directory"
  cat >"$directory/node-manifest.txt" <<EOF
role=restart
node_id=$receiver
offline_restart=PASS
network_attempted=false
process_exit_status=0
tip=$block_id
genesis=1111111111111111111111111111111111111111111111111111111111111111
candidate_root=2222222222222222222222222222222222222222222222222222222222222222
terminal_note_root=3333333333333333333333333333333333333333333333333333333333333333
parameters_sha256=4444444444444444444444444444444444444444444444444444444444444444
corpus_sha256=5555555555555555555555555555555555555555555555555555555555555555
tls_ca_sha256=ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
blockchain_commit=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
privacy_lab_commit=2222222222222222222222222222222222222222
network_id=1
circuit_version=1
root_height=100
EOF
  printf 'stage7_offline_restart=PASS node_id=%s\n' "$receiver" \
    >"$directory/node.log"
  write_host_manifest "$directory" 'stage7_restart_evidence=PASS'
done

bash "$bundle_validator" block "$work/rtx3060" "$work/rtx4070" \
  "$work/rtx3060-restart" "$work/rtx4070-restart" |
  grep -q '^stage7_evidence_bundle=PASS mode=block '

printf '%s\n' '-----BEGIN PRIVATE KEY-----' >"$work/rtx4070/leaked.key"
if bash "$bundle_validator" block "$work/rtx3060" "$work/rtx4070" \
    "$work/rtx3060-restart" "$work/rtx4070-restart" \
    >/dev/null 2>&1; then
  echo "bundle validator accepted private-key material" >&2
  exit 1
fi

printf 'stage7_evidence_bundle_validator=PASS\n'
