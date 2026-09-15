#!/usr/bin/env bash
set -euo pipefail

if (( $# != 8 )); then
  echo "usage: $0 OUTPUT ROLE NODE_MANIFEST NODE_LOG RESOURCE_LOG BINARY CERT CA" >&2
  exit 2
fi

output="$1"
role="$2"
node_manifest="$3"
node_log="$4"
resource_log="$5"
binary="$6"
certificate="$7"
ca_certificate="$8"
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

case "$role" in
  origin|relay|observer) pass_pattern="^stage7_private_load=PASS role=$role " ;;
  receiver) pass_pattern='^stage7_block_receiver=PASS ' ;;
  restart) pass_pattern='^stage7_offline_restart=PASS ' ;;
  *) echo "invalid role" >&2; exit 2 ;;
esac
for file in "$node_manifest" "$node_log" "$resource_log" "$binary" \
            "$certificate" "$ca_certificate"; do
  [[ -f "$file" && ! -L "$file" ]] || {
    echo "missing regular evidence input: $file" >&2
    exit 1
  }
done
grep -q "^role=$role$" "$node_manifest"
grep -q "$pass_pattern" "$node_log"
if grep -aE 'BEGIN .*PRIVATE KEY' "$node_manifest" "$node_log" \
    "$certificate" "$ca_certificate" >/dev/null; then
  echo "private-key material present in evidence input" >&2
  exit 1
fi
if grep -aEi '(^|[^[:alnum:]_])(mnemonic|secret_seed|wallet_seed|spending_key|payment_plaintext|rseed)[[:space:]]*[:=]' \
    "$node_manifest" "$node_log" "$resource_log" >/dev/null; then
  echo "secret seed or payment plaintext present in evidence input" >&2
  exit 1
fi
grep -q '^[[:space:]]*User time (seconds): ' "$resource_log"
grep -q '^[[:space:]]*System time (seconds): ' "$resource_log"
grep -q '^[[:space:]]*Maximum resident set size (kbytes): ' "$resource_log"
grep -q '^[[:space:]]*File system inputs: ' "$resource_log"
grep -q '^[[:space:]]*File system outputs: ' "$resource_log"
grep -q '^[[:space:]]*Exit status: 0$' "$resource_log"

if [[ -e "$output" && -n "$(find "$output" -mindepth 1 -print -quit 2>/dev/null)" ]]; then
  echo "refusing to replace non-empty evidence directory: $output" >&2
  exit 1
fi
mkdir -p "$output"
cp "$node_manifest" "$output/node-manifest.txt"
cp "$node_log" "$output/node.log"
cp "$resource_log" "$output/resource.log"
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
  printf 'resource_log_sha256=%s\n' \
    "$(sha256sum "$output/resource.log" | awk '{print $1}')"
  printf 'private_keys_included=false\n'
  case "$role" in
    origin|relay|observer)
      printf 'stage7_private_load_evidence=PASS role=%s\n' "$role"
      ;;
    receiver) printf 'stage7_block_evidence=PASS\n' ;;
    restart) printf 'stage7_restart_evidence=PASS\n' ;;
  esac
} | tee "$output/host-manifest.txt"
