#!/usr/bin/env bash
set -euo pipefail

# The GPU worker is intentionally kept outside the Onuros source tree because
# RavenCommunity/kawpowminer is GPLv3. Onuros nodes do not trust it; every share
# is independently recomputed by the pinned CPU consensus verifier.
readonly upstream='https://github.com/RavenCommunity/kawpowminer.git'
readonly commit='632f6ea0a5cd09e2c6443374dbe6db0a767715ba'
readonly source_dir="${1:-/workspace/kawpowminer-reference-nvidia}"
readonly build_dir="${source_dir}/build-onuros-nvidia"
readonly compat_include_dir="${build_dir}/onuros-compat"
readonly vector_ref_path='libdevcore/vector_ref.h'

for tool in git cmake make cmp sha256sum cut; do
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

if [[ "$(git -C "$source_dir" rev-parse HEAD)" != "$commit" ]]; then
  echo "worker checkout does not match pinned commit" >&2
  exit 1
fi
if ! git -C "$source_dir" diff --quiet "$commit" -- . \
        ":(exclude)${vector_ref_path}"; then
  echo "worker source has unexpected tracked modifications" >&2
  exit 1
fi
if ! git -C "$source_dir" submodule foreach --quiet --recursive \
        'git diff --quiet && git diff --cached --quiet'; then
  echo "worker submodule source has unexpected tracked modifications" >&2
  exit 1
fi

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

readonly vector_ref="${source_dir}/${vector_ref_path}"
readonly expected_vector_ref="${compat_include_dir}/vector_ref.expected.h"
git -C "$source_dir" show "${commit}:${vector_ref_path}" >"$expected_vector_ref"
sed -i '/^#include <vector>$/a #include <cstdint>' "$expected_vector_ref"
if ! git -C "$source_dir" diff --quiet "$commit" -- "$vector_ref_path" &&
   ! cmp -s "$vector_ref" "$expected_vector_ref"; then
  echo "worker vector_ref.h has an unexpected modification" >&2
  exit 1
fi
cp "$expected_vector_ref" "$vector_ref"

CPLUS_INCLUDE_PATH="${compat_include_dir}${CPLUS_INCLUDE_PATH:+:${CPLUS_INCLUDE_PATH}}" \
cmake -S "$source_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DETHASHCUDA=ON \
  -DETHASHCL=OFF \
  -DAPICORE=ON \
  -DCOMPUTE=86
CPLUS_INCLUDE_PATH="${compat_include_dir}${CPLUS_INCLUDE_PATH:+:${CPLUS_INCLUDE_PATH}}" \
cmake --build "$build_dir" --parallel "$(nproc)"

miner="$(find "$build_dir" -type f -name kawpowminer -perm -u+x -print -quit)"
if [[ -z "$miner" ]]; then
  echo "build finished but kawpowminer executable was not found" >&2
  exit 1
fi

echo "nvidia_reference_build=PASS"
echo "upstream_commit=$commit"
printf 'compatibility_diff_sha256='
git -C "$source_dir" diff --binary -- "$vector_ref_path" | sha256sum | cut -d' ' -f1
echo "miner=$miner"
sha256sum "$miner"
