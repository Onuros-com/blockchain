#!/usr/bin/env bash
set -euo pipefail

echo "archived Orchard/Halo2 duration gate: not valid for the active protocol" >&2
exit 64

workers="${1:-4}"
seconds="${2:-600}"
minimum_tps="${3:-100}"
case "$workers:$seconds:$minimum_tps" in
  *[!0-9.:]*|0:*|*:0:*|*:*:0) echo "usage: $0 [workers] [seconds] [minimum-tps]" >&2; exit 2 ;;
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
fixture="$run_dir/orchard-fixture.bin"
start_file="$run_dir/start"
"$binary" --generate "$fixture"

for ((worker = 0; worker < workers; ++worker)); do
  "$binary" --verify-duration "$fixture" "$seconds" \
    "$run_dir/ready-$worker" "$start_file" \
    >"$run_dir/result-$worker" 2>"$run_dir/error-$worker" &
  pids+=("$!")
done

deadline=$((SECONDS + 900))
while :; do
  ready_count="$(find "$run_dir" -maxdepth 1 -name 'ready-*' -type f | wc -l)"
  if ((ready_count == workers)); then break; fi
  if ((SECONDS >= deadline)); then
    echo "workers did not finish Orchard setup within 15 minutes" >&2
    exit 1
  fi
  sleep 1
done

started_ns="$(date +%s%N)"
touch "$start_file"
for pid in "${pids[@]}"; do wait "$pid"; done
finished_ns="$(date +%s%N)"
pids=()

verified="$(awk -F'[ =]' '/^verified=/{total += $2} END {print total + 0}' \
  "$run_dir"/result-*)"
elapsed_ns=$((finished_ns - started_ns))
aggregate_tps="$(awk -v count="$verified" -v ns="$elapsed_ns" \
  'BEGIN { printf "%.3f", count / (ns / 1000000000) }')"
cat "$run_dir"/result-*
printf 'gate=real-orchard-verification duration_target=%s verified=%s workers=%s elapsed_seconds=%.9f aggregate_tps=%s minimum_tps=%s\n' \
  "$seconds" "$verified" "$workers" \
  "$(awk -v ns="$elapsed_ns" 'BEGIN { print ns / 1000000000 }')" \
  "$aggregate_tps" "$minimum_tps"

awk -v actual="$aggregate_tps" -v minimum="$minimum_tps" \
  'BEGIN { exit(actual + 0 >= minimum + 0 ? 0 : 1) }'
