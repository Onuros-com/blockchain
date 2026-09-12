#!/usr/bin/env bash
set -euo pipefail

output="${1:?usage: capture-stage7-tls-evidence.sh OUTPUT_DIR ROLE BINARY CERT CA LOG}"
role="${2:?missing role}"
binary="${3:?missing probe binary}"
certificate="${4:?missing peer certificate}"
ca_certificate="${5:?missing CA certificate}"
probe_log="${6:?missing probe log}"

case "$role" in server|client) ;; *) echo "role must be server or client" >&2; exit 2 ;; esac
for path in "$binary" "$certificate" "$ca_certificate" "$probe_log"; do
  [[ -f "$path" ]] || { echo "missing evidence input: $path" >&2; exit 1; }
done
grep -q "^tls_${role}=PASS " "$probe_log" || {
  echo "probe log has no successful $role result" >&2
  exit 1
}

mkdir -p "$output"
manifest="$output/${role}-manifest.txt"
{
  date -u
  grep '^PRETTY_NAME=' /etc/os-release 2>/dev/null || true
  uname -a
  printf 'role=%s\n' "$role"
  if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    printf 'commit=%s\n' "$(git rev-parse HEAD)"
  fi
  openssl version
  openssl x509 -in "$certificate" -noout -subject -issuer -dates -fingerprint -sha256
  printf 'certificate_san='
  openssl x509 -in "$certificate" -noout -ext subjectAltName |
    tail -n +2 | tr -d ' \n'
  printf '\n'
  sha256sum "$binary" "$certificate" "$ca_certificate" "$probe_log"
  printf 'private_keys_included=false\n'
  printf 'independent_tls_evidence=PASS role=%s\n' "$role"
} | tee "$manifest"
cp "$probe_log" "$output/${role}-probe.log"
printf 'evidence_manifest=%s\n' "$manifest"
