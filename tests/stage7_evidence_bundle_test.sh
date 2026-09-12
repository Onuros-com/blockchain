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
divergent_transactions=0
limits_exceeded=0
verification_backend=orchard-ffi
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
block_bytes=16612467
transactions=1811
mempool_overlap_percent=50
mempool_overlap_transactions=905
announcement_bytes=58168
request_bytes=3728
response_bytes=8300000
propagation_validation_seconds=29.999
block_id=$block_id
limits_exceeded=0
validation=PASS
durable_activation=PASS
restart_recovery=PASS
verification_backend=orchard-ffi
process_exit_status=0
private_payloads_logged=false
EOF
  printf 'stage7_block_receiver=PASS receiver_id=%s\n' "$receiver" \
    >"$directory/node.log"
  write_host_manifest "$directory" 'stage7_block_evidence=PASS'
done

bash "$bundle_validator" block "$work/rtx3060" "$work/rtx4070" |
  grep -q '^stage7_evidence_bundle=PASS mode=block '

printf '%s\n' '-----BEGIN PRIVATE KEY-----' >"$work/rtx4070/leaked.key"
if bash "$bundle_validator" block "$work/rtx3060" "$work/rtx4070" \
    >/dev/null 2>&1; then
  echo "bundle validator accepted private-key material" >&2
  exit 1
fi

printf 'stage7_evidence_bundle_validator=PASS\n'
