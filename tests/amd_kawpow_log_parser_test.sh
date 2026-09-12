#!/usr/bin/env bash
set -euo pipefail

readonly validator="${1:?usage: amd_kawpow_log_parser_test.sh VALIDATOR}"
readonly work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT

expect_pass() {
  "$validator" "$1" >/dev/null
}

expect_fail() {
  if "$validator" "$1" >/dev/null 2>&1; then
    echo "unexpected parser pass: $1" >&2
    exit 1
  fi
}

printf '%s\n' \
  'cl-0 Using PciId : 26:00.0 gfx942:sramecc+:xnack- OpenCL 2.0' \
  'kawpowminer A1 8.78 Mh - cl0 8.78' >"$work/valid.log"
printf '%s\n' \
  'cl-0 Using PciId : 26:00.0 gfx942 OpenCL 2.0' \
  'kawpowminer A0 0.00 Mh - cl0 0.00' >"$work/zero.log"
printf '%s\n' \
  'cl-0 Using CPU OpenCL 3.0' \
  'kawpowminer A1 99.0 Mh - cl0 99.0' >"$work/cpu.log"
printf '%s\n' \
  'cl-0 Using PciId : 26:00.0 gfx942 OpenCL 2.0' \
  'kawpowminer A1 8.78 Mh - cl0 8.78' \
  'Fatal device error' >"$work/fatal.log"

expect_pass "$work/valid.log"
expect_fail "$work/zero.log"
expect_fail "$work/cpu.log"
expect_fail "$work/fatal.log"

echo "4 AMD KawPoW log-parser checks passed"
