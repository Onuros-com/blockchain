#!/usr/bin/env bash
set -euo pipefail

readonly log="${1:?usage: validate-amd-kawpow-log.sh LOG}"

if [[ ! -f "$log" ]]; then
  echo "amd_kawpow_log=FAIL reason=missing_log" >&2
  exit 2
fi
if grep -Eqi 'no usable mining devices|fatal|segmentation fault' "$log"; then
  echo "amd_kawpow_log=FAIL reason=runtime_error"
  exit 1
fi
if ! grep -Eqi 'Using PciId.*gfx[0-9a-f]+.*OpenCL' "$log"; then
  echo "amd_kawpow_log=FAIL reason=no_amd_gpu_evidence"
  exit 1
fi
if ! grep -Eio '[0-9]+([.][0-9]+)?[[:space:]]*[kmg]?h(/s)?' "$log" |
     awk '$1 + 0 > 0 { found = 1 } END { exit found ? 0 : 1 }'; then
  echo "amd_kawpow_log=FAIL reason=no_nonzero_hashrate_evidence"
  exit 1
fi

echo "amd_kawpow_log=PASS"
