#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

active_surfaces=(
  "$repo_dir/CMakeLists.txt"
  "$repo_dir/Makefile"
  "$repo_dir/.github/workflows"
  "$repo_dir/apps/stage7_private_load_node.cpp"
  "$repo_dir/apps/stage7_block_propagation_node.cpp"
  "$repo_dir/scripts/build-stage7-private-load.sh"
  "$repo_dir/scripts/run-stage7-private-load-loopback.sh"
  "$repo_dir/scripts/validate-stage7-performance-evidence.sh"
)

if grep -R -n -i -E -- 'orchard|halo[[:space:]]*2|onuros_orchard' "${active_surfaces[@]}"; then
  echo "active build, runtime, workflow or qualification surface references Orchard/Halo2" >&2
  exit 1
fi

grep -qF 'proof_system=groth16-bls12-381' \
  "$repo_dir/apps/stage7_private_load_node.cpp"
grep -qF 'commitment_hash=poseidon' \
  "$repo_dir/apps/stage7_private_load_node.cpp"
grep -qF 'ONUROS_PRIVACY_ENGINE_LIBRARY' "$repo_dir/CMakeLists.txt"
grep -qF 'Onuros-com/Onuros-privacy-lab' \
  "$repo_dir/.github/workflows/privacy-engine-bridge.yml"
grep -qF 'active_privacy_protocol_qualified=false' \
  "$repo_dir/docs/evidence/stage7-gcp-orchard-duration-20260912.txt"
grep -qF 'superseded-historical' \
  "$repo_dir/docs/evidence/stage7-gcp-orchard-duration-20260912.txt"
grep -qF 'Orchard/Halo2 is no longer built, run, or selected by default' \
  "$repo_dir/README.md"

printf 'active_privacy_protocol_audit=PASS protocol=groth16-poseidon historical_evidence=preserved\n'
