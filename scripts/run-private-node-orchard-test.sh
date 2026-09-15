#!/usr/bin/env bash
set -euo pipefail

echo "archived Orchard/Halo2 pipeline: disabled on the active migration branch" >&2
exit 64

if (( $# != 2 )); then
  echo "usage: $0 PRIVATE_VERIFY_BENCHMARK PIPELINE_TEST" >&2
  exit 2
fi

generator="$1"
pipeline="$2"
run_dir="$(mktemp -d)"
cleanup() { rm -rf -- "$run_dir"; }
trap cleanup EXIT INT TERM

template="$run_dir/template.onp2"
digest="$run_dir/digest.bin"
signed="$run_dir/signed.onp2"

"$generator" --generate-onuros "$template"
"$pipeline" --write-digest "$template" "$digest"
"$generator" --generate-onuros "$signed" "$digest"
"$pipeline" --run "$signed"

echo "real Orchard private-node pipeline passed"
