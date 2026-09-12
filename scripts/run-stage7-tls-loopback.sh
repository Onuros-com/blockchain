#!/usr/bin/env bash
set -euo pipefail

binary="${1:-./build/tls_transport_test}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
  -subj '/CN=Onuros Stage7 Test CA' -keyout "$work/ca.key" \
  -out "$work/ca.crt" >/dev/null 2>&1
for peer in server client; do
  openssl req -newkey rsa:2048 -nodes -sha256 -subj "/CN=onuros-$peer" \
    -keyout "$work/$peer.key" -out "$work/$peer.csr" >/dev/null 2>&1
  openssl x509 -req -days 1 -sha256 -in "$work/$peer.csr" \
    -CA "$work/ca.crt" -CAkey "$work/ca.key" -CAcreateserial \
    -out "$work/$peer.crt" >/dev/null 2>&1
done

"$binary" "$work/server.crt" "$work/server.key" \
  "$work/client.crt" "$work/client.key" "$work/ca.crt"
