#!/usr/bin/env bash
set -euo pipefail

readonly miner="${1:?usage: run-amd-kawpow-gate.sh MINER [SECONDS] [BLOCK] [EVIDENCE_DIR]}"
readonly duration="${2:-600}"
readonly block="${3:-30000}"
readonly evidence_dir="${4:-/workspace/onuros-stage4-evidence}"

if [[ ! -x "$miner" || ! "$duration" =~ ^[1-9][0-9]*$ ||
      ! "$block" =~ ^[0-9]+$ ]]; then
  echo "invalid miner, duration or block" >&2
  exit 2
fi

mkdir -p "$evidence_dir"
readonly log="$evidence_dir/amd-kawpow-${duration}s.log"
readonly manifest="$evidence_dir/amd-kawpow-manifest.txt"

{
  date -u
  grep '^PRETTY_NAME=' /etc/os-release
  uname -a
  amd-smi version
  amd-smi list
  command -v clinfo >/dev/null && clinfo | grep -E 'Platform Name|Device Name|Device Version' | head -20 || true
  sha256sum "$miner"
  printf 'command=timeout --signal=INT --kill-after=30s %ss %q -G -M %s --display-interval 10\n' \
    "$duration" "$miner" "$block"
} 2>&1 | tee "$manifest"

set +e
timeout --signal=INT --kill-after=30s "${duration}s" \
  "$miner" -G -M "$block" --display-interval 10 2>&1 | tee "$log"
status=${PIPESTATUS[0]}
set -e

{
  echo "miner_exit_status=$status"
  date -u
  amd-smi metric || true
  sha256sum "$log"
} 2>&1 | tee -a "$manifest"

if [[ "$status" -ne 124 ]]; then
  echo "amd_kawpow_gate=FAIL reason=duration_not_completed" | tee -a "$manifest"
  exit 1
fi
readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if ! "$script_dir/validate-amd-kawpow-log.sh" "$log" | tee -a "$manifest"; then
  echo "amd_kawpow_gate=FAIL reason=log_validation" | tee -a "$manifest"
  exit 1
fi

echo "amd_kawpow_gate=PASS duration_seconds=$duration block=$block" | tee -a "$manifest"
