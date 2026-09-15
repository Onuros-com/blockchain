#!/usr/bin/env bash
set -euo pipefail

capture="${1:?capture script required}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

printf '%s\n' 'role=origin' >"$work/node.manifest"
printf '%s\n' 'stage7_private_load=PASS role=origin admitted_unique=66000' \
  >"$work/node.log"
cat >"$work/resource.log" <<'EOF'
	User time (seconds): 10.00
	System time (seconds): 1.00
	Maximum resident set size (kbytes): 4096
	File system inputs: 0
	File system outputs: 8
	Exit status: 0
EOF
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=stage7-test \
  -keyout "$work/private.key" -out "$work/node.crt" >/dev/null 2>&1

bash "$capture" "$work/output" origin "$work/node.manifest" \
  "$work/node.log" "$work/resource.log" /bin/true "$work/node.crt" \
  "$work/node.crt" >/dev/null
test -f "$work/output/node-manifest.txt"
test -f "$work/output/node.log"
test -f "$work/output/resource.log"
grep -q '^resource_log_sha256=[0-9a-f]\{64\}$' \
  "$work/output/host-manifest.txt"

printf '%s\n' 'secret_seed=must-not-escape' >>"$work/node.log"
if bash "$capture" "$work/rejected" origin "$work/node.manifest" \
    "$work/node.log" "$work/resource.log" /bin/true "$work/node.crt" \
    "$work/node.crt" >/dev/null 2>&1; then
  echo "capture accepted secret seed material" >&2
  exit 1
fi

printf '%s\n' 'stage7_private_load=PASS role=origin admitted_unique=66000' \
  '-----BEGIN PRIVATE KEY-----' >"$work/node.log"
if bash "$capture" "$work/rejected-key" origin "$work/node.manifest" \
    "$work/node.log" "$work/resource.log" /bin/true "$work/node.crt" \
    "$work/node.crt" >/dev/null 2>&1; then
  echo "capture accepted private-key material" >&2
  exit 1
fi

printf 'stage7_capture_evidence=PASS\n'
