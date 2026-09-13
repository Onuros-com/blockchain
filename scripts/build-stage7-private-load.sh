#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$repo_dir/build-stage7-private-load}"
manifest="$repo_dir/crypto/orchard-ffi/Cargo.toml"
ffi="$repo_dir/crypto/orchard-ffi/target/release/libonuros_orchard_ffi.so"

for tool in cargo cmake; do
  command -v "$tool" >/dev/null || {
    echo "required tool is missing: $tool" >&2
    exit 1
  }
done

cargo build --locked --release --manifest-path "$manifest" \
  --lib --bin private_verify_benchmark
cmake -S "$repo_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
  -DONUROS_ORCHARD_FFI_LIBRARY="$ffi"
cmake --build "$build_dir" --parallel "${ONUROS_BUILD_JOBS:-$(nproc)}" \
  --target onuros_stage7_private_load_node \
           onuros_stage7_block_propagation_node \
           onuros_stage7_network_node

printf 'stage7_private_load_build=PASS\n'
printf 'node=%s\n' "$build_dir/onuros_stage7_private_load_node"
printf 'block_node=%s\n' \
  "$build_dir/onuros_stage7_block_propagation_node"
printf 'sync_node=%s\n' "$build_dir/onuros_stage7_network_node"
printf 'corpus_generator=%s\n' \
  "$repo_dir/crypto/orchard-ffi/target/release/private_verify_benchmark"
