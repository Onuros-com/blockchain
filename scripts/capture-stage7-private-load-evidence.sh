#!/usr/bin/env bash
set -euo pipefail

if (( $# != 7 )); then
  echo "usage: $0 OUTPUT ROLE NODE_MANIFEST NODE_LOG BINARY CERT CA" >&2
  exit 2
fi

output="$1"
role="$2"
node_manifest="$3"
node_log="$4"
binary="$5"
certificate="$6"
ca_certificate="$7"
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

case "$role" in origin|relay|observer) ;; *) echo "invalid role" >&2; exit 2 ;; esac
for file in "$node_manifest" "$node_log" "$binary" "$certificate" "$ca_certificate"; do
  [[ -f "$file" ]] || { echo "missing evidence input: $file" >&2; exit 1; }
done
grep -q "^role=$role$" "$node_manifest"
grep -q "^stage7_private_load=PASS role=$role " "$node_log"
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
  printf 'role=%s\n' "$role"
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
  printf 'stage7_private_load_evidence=PASS role=%s\n' "$role"
} | tee "$output/host-manifest.txt"
