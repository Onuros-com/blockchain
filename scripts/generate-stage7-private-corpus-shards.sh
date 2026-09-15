#!/usr/bin/env bash
set -euo pipefail

echo "archived Orchard/Halo2 corpus sharding: use the pinned Privacy Lab corpus" >&2
exit 64

usage() {
  echo "usage:" >&2
  echo "  $0 generate OUTPUT TRANSACTIONS SHARDS FIRST_SHARD END_SHARD" >&2
  echo "  $0 merge OUTPUT TRANSACTIONS SHARDS" >&2
  exit 2
}

(( $# >= 1 )) || usage
mode="$1"
shift

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
generator="$repo_dir/crypto/orchard-ffi/target/release/private_verify_benchmark"

build_generator() {
  cargo build --locked --release \
    --manifest-path "$repo_dir/crypto/orchard-ffi/Cargo.toml" \
    --bin private_verify_benchmark
}

require_positive_integer() {
  local name="$1" value="$2"
  [[ "$value" =~ ^[1-9][0-9]*$ ]] || {
    echo "invalid $name: $value" >&2
    exit 2
  }
}

case "$mode" in
  generate)
    (( $# == 5 )) || usage
    output="$1"
    transactions="$2"
    shards="$3"
    first="$4"
    end="$5"
    require_positive_integer transactions "$transactions"
    require_positive_integer shards "$shards"
    [[ "$first" =~ ^[0-9]+$ && "$end" =~ ^[1-9][0-9]*$ ]] || usage
    (( first < end && end <= shards && shards <= transactions )) || {
      echo "invalid shard range [$first,$end) for $shards shards" >&2
      exit 2
    }
    [[ ! -e "$output" ]] || {
      echo "refusing to create shards beside existing corpus: $output" >&2
      exit 1
    }
    mkdir -p "$(dirname "$output")"
    build_generator
    started="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    "$generator" --generate-corpus-shards \
      "$output" "$transactions" "$shards" "$first" "$end"
    finished="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    manifest="$output.shards-$first-$end.manifest.txt"
    {
      printf 'corpus_shard_generation=PASS\n'
      printf 'utc_started=%s\n' "$started"
      printf 'utc_finished=%s\n' "$finished"
      printf 'transactions=%s\n' "$transactions"
      printf 'shards=%s\n' "$shards"
      printf 'first_shard=%s\n' "$first"
      printf 'end_shard=%s\n' "$end"
      printf 'repository_commit=%s\n' "$(git -C "$repo_dir" rev-parse HEAD)"
      for (( shard = first; shard < end; ++shard )); do
        printf -v part '%s.part-%03d' "$output" "$shard"
        sha256sum "$part"
      done
      sha256sum "$generator"
      printf 'contains_spending_keys=false\n'
      printf 'publish_corpus=false\n'
    } | tee "$manifest"
    ;;
  merge)
    (( $# == 3 )) || usage
    output="$1"
    transactions="$2"
    shards="$3"
    require_positive_integer transactions "$transactions"
    require_positive_integer shards "$shards"
    (( shards <= transactions )) || {
      echo "shard count exceeds transaction count" >&2
      exit 2
    }
    [[ ! -e "$output" ]] || {
      echo "refusing to overwrite existing corpus: $output" >&2
      exit 1
    }
    for (( shard = 0; shard < shards; ++shard )); do
      printf -v part '%s.part-%03d' "$output" "$shard"
      [[ -f "$part" ]] || {
        echo "missing corpus shard: $part" >&2
        exit 1
      }
    done
    build_generator
    started="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    "$generator" --merge-corpus-shards "$output" "$transactions" "$shards"
    finished="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    {
      printf 'corpus_merge=PASS\n'
      printf 'utc_started=%s\n' "$started"
      printf 'utc_finished=%s\n' "$finished"
      printf 'transactions=%s\n' "$transactions"
      printf 'shards=%s\n' "$shards"
      printf 'corpus_bytes=%s\n' "$(stat -c %s "$output")"
      printf 'repository_commit=%s\n' "$(git -C "$repo_dir" rev-parse HEAD)"
      sha256sum "$output" "$generator"
      printf 'contains_spending_keys=false\n'
      printf 'publish_corpus=false\n'
    } | tee "$output.manifest.txt"
    ;;
  *) usage ;;
esac
