#!/usr/bin/env bash
set -euo pipefail

if (( $# != 6 )); then
  echo "usage: $0 OUTPUT RECEIVER_MANIFEST RECEIVER_LOG BINARY CERT CA" >&2
  exit 2
fi

output="$1"
node_manifest="$2"
node_log="$3"
binary="$4"
certificate="$5"
ca_certificate="$6"
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

for file in "$node_manifest" "$node_log" "$binary" "$certificate" \
            "$ca_certificate"; do
  [[ -f "$file" ]] || { echo "missing evidence input: $file" >&2; exit 1; }
done
grep -q '^role=receiver$' "$node_manifest"
grep -q '^validation=PASS$' "$node_manifest"
grep -q '^durable_activation=PASS$' "$node_manifest"
grep -q '^restart_recovery=PASS$' "$node_manifest"
grep -q '^stage7_block_receiver=PASS ' "$node_log"
! grep -aE 'BEGIN .*PRIVATE KEY' "$node_manifest" "$node_log" \
  "$certificate" "$ca_certificate" >/dev/null

mkdir -p "$output"
cp "$node_manifest" "$output/node-manifest.txt"
cp "$node_log" "$output/node.log"
{
  date -u
  grep '^PRETTY_NAME=' /etc/os-release
  uname -a
  command -v lscpu >/dev/null && lscpu | \
    grep -E '^(CPU\(s\)|Model name|Thread\(s\) per core|Core\(s\) per socket|Socket\(s\)):' || true
  command -v free >/dev/null && free -h || true
  printf 'commit=%s\n' "$(git -C "$repo_dir" rev-parse HEAD)"
  openssl version
  openssl x509 -in "$certificate" -noout -subject -issuer -dates \
    -fingerprint -sha256 -ext subjectAltName
  printf 'binary_sha256=%s\n' "$(sha256sum "$binary" | awk '{print $1}')"
  printf 'certificate_sha256=%s\n' \
    "$(sha256sum "$certificate" | awk '{print $1}')"
  printf 'ca_certificate_sha256=%s\n' \
    "$(sha256sum "$ca_certificate" | awk '{print $1}')"
  printf 'node_manifest_sha256=%s\n' \
    "$(sha256sum "$output/node-manifest.txt" | awk '{print $1}')"
  printf 'node_log_sha256=%s\n' \
    "$(sha256sum "$output/node.log" | awk '{print $1}')"
  printf 'private_keys_included=false\n'
  printf 'stage7_block_evidence=PASS\n'
} | tee "$output/host-manifest.txt"
