#!/usr/bin/env bash
set -euo pipefail

workers="${1:-$(nproc)}"
iterations="${2:-100}"
case "$workers:$iterations" in
  *[!0-9:]*|0:*|*:0) echo "usage: $0 [positive-workers] [positive-iterations]" >&2; exit 2 ;;
esac

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
manifest="$repo_dir/crypto/orchard-ffi/Cargo.toml"
binary="$repo_dir/crypto/orchard-ffi/target/release/private_verify_benchmark"
run_dir="$(mktemp -d)"
pids=()
cleanup() {
  for pid in "${pids[@]:-}"; do kill "$pid" 2>/dev/null || true; done
  rm -rf -- "$run_dir"
}
trap cleanup EXIT INT TERM

cargo build --locked --release --manifest-path "$manifest" \
  --bin private_verify_benchmark

start_file="$run_dir/start"
for ((worker = 0; worker < workers; ++worker)); do
  "$binary" "$iterations" "$run_dir/ready-$worker" "$start_file" \
    >"$run_dir/result-$worker" 2>"$run_dir/error-$worker" &
  pids+=("$!")
done

deadline=$((SECONDS + 900))
while :; do
  ready_count="$(find "$run_dir" -maxdepth 1 -name 'ready-*' -type f | wc -l)"
  if ((ready_count == workers)); then break; fi
  if ((SECONDS >= deadline)); then
    echo "workers did not finish proof setup within 15 minutes" >&2
    exit 1
  fi
  sleep 1
done

started_ns="$(date +%s%N)"
touch "$start_file"
for pid in "${pids[@]}"; do wait "$pid"; done
finished_ns="$(date +%s%N)"
pids=()

verified=$((workers * iterations))
elapsed_ns=$((finished_ns - started_ns))
aggregate_tps="$(awk -v count="$verified" -v ns="$elapsed_ns" \
  'BEGIN { printf "%.3f", count / (ns / 1000000000) }')"

cat "$run_dir"/result-*
printf 'aggregate_verified=%s workers=%s elapsed_seconds=%.9f aggregate_tps=%s\n' \
  "$verified" "$workers" "$(awk -v ns="$elapsed_ns" 'BEGIN { print ns / 1000000000 }')" \
  "$aggregate_tps"
