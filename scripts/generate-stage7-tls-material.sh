#!/usr/bin/env bash
set -euo pipefail

output="${1:?usage: generate-stage7-tls-material.sh OUTPUT_DIR SERVER_DNS CLIENT_DNS}"
server_dns="${2:?missing server DNS identity}"
client_dns="${3:?missing client DNS identity}"

if [[ -e "$output" && -n "$(find "$output" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]]; then
  echo "refusing to replace non-empty TLS directory: $output" >&2
  exit 1
fi
mkdir -p "$output"
umask 077

openssl req -x509 -newkey rsa:3072 -nodes -days 365 -sha256 \
  -subj '/CN=Onuros Stage 7 Private CA' \
  -keyout "$output/ca.key" -out "$output/ca.crt" >/dev/null 2>&1

issue_peer() {
  local role="$1"
  local dns="$2"
  local usage="$3"
  openssl req -newkey rsa:3072 -nodes -sha256 -subj "/CN=$dns" \
    -keyout "$output/$role.key" -out "$output/$role.csr" >/dev/null 2>&1
  {
    printf 'subjectAltName=DNS:%s\n' "$dns"
    printf 'extendedKeyUsage=%s\n' "$usage"
    printf 'keyUsage=digitalSignature,keyEncipherment\n'
  } > "$output/$role.ext"
  openssl x509 -req -days 90 -sha256 -in "$output/$role.csr" \
    -CA "$output/ca.crt" -CAkey "$output/ca.key" -CAcreateserial \
    -extfile "$output/$role.ext" -out "$output/$role.crt" >/dev/null 2>&1
  openssl verify -CAfile "$output/ca.crt" "$output/$role.crt"
  rm -f "$output/$role.csr" "$output/$role.ext"
}

issue_peer server "$server_dns" serverAuth
issue_peer client "$client_dns" clientAuth
chmod 600 "$output"/*.key
chmod 644 "$output"/*.crt
printf 'tls_material=READY server_dns=%s client_dns=%s directory=%s\n' \
  "$server_dns" "$client_dns" "$output"
