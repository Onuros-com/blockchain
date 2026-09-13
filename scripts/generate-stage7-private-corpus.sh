#!/usr/bin/env bash
set -euo pipefail

if (( $# < 1 || $# > 3 )); then
  echo "usage: $0 OUTPUT [TRANSACTIONS=66000] [WORKERS=nproc]" >&2
  exit 2
fi

output="$1"
transactions="${2:-66000}"
workers="${3:-$(nproc)}"
case "$transactions:$workers" in
  *[!0-9:]*|0:*|*:0) echo "invalid transaction or worker count" >&2; exit 2 ;;
esac

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
generator="$repo_dir/crypto/orchard-ffi/target/release/private_verify_benchmark"
output_dir="$(dirname "$output")"
mkdir -p "$output_dir"

if [[ -e "$output" ]]; then
  echo "refusing to overwrite existing corpus: $output" >&2
  exit 1
fi

cargo build --locked --release \
  --manifest-path "$repo_dir/crypto/orchard-ffi/Cargo.toml" \
  --bin private_verify_benchmark

started="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
"$generator" --generate-corpus "$output" "$transactions" "$workers"
finished="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

{
  printf 'corpus_generation=PASS\n'
  printf 'utc_started=%s\n' "$started"
  printf 'utc_finished=%s\n' "$finished"
  printf 'transactions=%s\n' "$transactions"
  printf 'workers=%s\n' "$workers"
  printf 'resumable_shards=true\n'
  printf 'corpus_bytes=%s\n' "$(stat -c %s "$output")"
  printf 'repository_commit=%s\n' "$(git -C "$repo_dir" rev-parse HEAD)"
  sha256sum "$output" "$generator"
  printf 'contains_spending_keys=false\n'
  printf 'publish_corpus=false\n'
} | tee "$output.manifest.txt"
