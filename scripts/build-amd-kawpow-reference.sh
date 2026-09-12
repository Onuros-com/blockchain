#!/usr/bin/env bash
set -euo pipefail

# The GPU worker is intentionally kept outside the Onuros source tree because
# RavenCommunity/kawpowminer is GPLv3. Onuros nodes do not trust it; every share
# is independently recomputed by the pinned CPU consensus verifier.
readonly upstream='https://github.com/RavenCommunity/kawpowminer.git'
readonly commit='632f6ea0a5cd09e2c6443374dbe6db0a767715ba'
readonly source_dir="${1:-/workspace/kawpowminer-reference}"
readonly build_dir="${source_dir}/build-onuros-amd"
readonly compat_include_dir="${build_dir}/onuros-compat"

for tool in git cmake make; do
  if ! command -v "$tool" >/dev/null; then
    echo "missing build tool: $tool" >&2
    exit 2
  fi
done

if [[ ! -d "${source_dir}/.git" ]]; then
  if [[ -e "$source_dir" ]]; then
    echo "refusing to overwrite non-git path: $source_dir" >&2
    exit 2
  fi
  git clone "$upstream" "$source_dir"
fi

actual_origin="$(git -C "$source_dir" remote get-url origin)"
if [[ "$actual_origin" != "$upstream" ]]; then
  echo "unexpected origin: $actual_origin" >&2
  exit 2
fi

git -C "$source_dir" fetch origin "$commit"
git -C "$source_dir" checkout --detach "$commit"
git -C "$source_dir" submodule update --init --recursive

# This pinned 2019 worker predates dynamic PTHREAD_STACK_MIN in modern glibc
# and transitive <cstdint> includes being removed by modern libstdc++.
mkdir -p "$compat_include_dir"
cat >"${compat_include_dir}/pthread.h" <<'EOF'
#pragma once
#ifndef PTHREAD_STACK_MIN
#define PTHREAD_STACK_MIN 16384
#endif
#include_next <pthread.h>
EOF

readonly vector_ref="${source_dir}/libdevcore/vector_ref.h"
if ! grep -Eq '^#include <cstdint>$' "$vector_ref"; then
  sed -i '/^#include <vector>$/a #include <cstdint>' "$vector_ref"
fi

CPLUS_INCLUDE_PATH="${compat_include_dir}${CPLUS_INCLUDE_PATH:+:${CPLUS_INCLUDE_PATH}}" \
cmake -S "$source_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DETHASHCUDA=OFF \
  -DETHASHCL=ON \
  -DAPICORE=ON
CPLUS_INCLUDE_PATH="${compat_include_dir}${CPLUS_INCLUDE_PATH:+:${CPLUS_INCLUDE_PATH}}" \
cmake --build "$build_dir" --parallel "$(nproc)"

miner="$(find "$build_dir" -type f -name kawpowminer -perm -u+x -print -quit)"
if [[ -z "$miner" ]]; then
  echo "build finished but kawpowminer executable was not found" >&2
  exit 1
fi

echo "amd_reference_build=PASS"
echo "upstream_commit=$commit"
echo "miner=$miner"
sha256sum "$miner"
