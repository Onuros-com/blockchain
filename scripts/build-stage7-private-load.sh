#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$repo_dir/build-stage7-private-load}"
privacy_lab_dir="${ONUROS_PRIVACY_LAB_DIR:?set ONUROS_PRIVACY_LAB_DIR to a pinned Onuros-privacy-lab checkout}"
manifest="$privacy_lab_dir/prototypes/privacy-engine-ffi/Cargo.toml"

case "$(uname -s)" in
  Linux*) ffi="$privacy_lab_dir/prototypes/privacy-engine-ffi/target/release/libonuros_privacy_engine_ffi.so" ;;
  Darwin*) ffi="$privacy_lab_dir/prototypes/privacy-engine-ffi/target/release/libonuros_privacy_engine_ffi.dylib" ;;
  *) echo "use CMake directly with the Windows privacy_engine_ffi library" >&2; exit 2 ;;
esac

for tool in cargo cmake; do
  command -v "$tool" >/dev/null || {
    echo "required tool is missing: $tool" >&2
    exit 1
  }
done

cargo +1.98.1 build --locked --release --manifest-path "$manifest" --lib
cmake -S "$repo_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
  -DONUROS_PRIVACY_ENGINE_LIBRARY="$ffi"
cmake --build "$build_dir" --parallel "${ONUROS_BUILD_JOBS:-$(nproc)}" \
  --target onuros_stage7_private_load_node \
           onuros_stage7_candidate_sync_node \
           onuros_stage7_network_node

printf 'stage7_private_load_build=PASS\n'
printf 'proof_system=groth16-bls12-381\n'
printf 'commitment_hash=poseidon\n'
printf 'node=%s\n' "$build_dir/onuros_stage7_private_load_node"
printf 'candidate_sync_node=%s\n' \
  "$build_dir/onuros_stage7_candidate_sync_node"
printf 'transport_fixture=%s\n' "$build_dir/onuros_stage7_network_node"
printf 'privacy_engine=%s\n' "$ffi"
printf 'corpus_generator=%s\n' \
  "$privacy_lab_dir/prototypes/hash-compare/target/release/unique-batch-smoke"
