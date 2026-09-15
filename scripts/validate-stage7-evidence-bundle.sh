#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
manifest_validator="$repo_dir/scripts/validate-stage7-performance-evidence.sh"

fail() {
  echo "stage7 evidence bundle rejected: $*" >&2
  exit 1
}

field() {
  local file="$1" key="$2"
  awk -F= -v key="$key" '
    $1 == key { value=$2; count++ }
    END { if (count != 1 || value == "") exit 1; print value }
  ' "$file"
}

require_hash() {
  local value="$1" label="$2"
  [[ "$value" =~ ^[0-9a-f]{64}$ ]] || fail "invalid $label"
}

check_bundle() {
  local mode="$1" expected_role="$2" directory="$3"
  [[ -d "$directory" && ! -L "$directory" ]] || \
    fail "missing evidence directory: $directory"
  local node_manifest="$directory/node-manifest.txt"
  local node_log="$directory/node.log"
  local host_manifest="$directory/host-manifest.txt"
  for file in "$node_manifest" "$node_log" "$host_manifest"; do
    [[ -f "$file" && ! -L "$file" ]] || fail "missing regular file: $file"
  done
  if find "$directory" -type l -print -quit | grep -q .; then
    fail "symbolic link present in $directory"
  fi
  if find "$directory" -type f \( -name '*.key' -o -name '*.p12' \
      -o -name '*.pfx' \) -print -quit | grep -q .; then
    fail "private-key file present in $directory"
  fi
  if grep -aER 'BEGIN .*PRIVATE KEY' "$directory" >/dev/null; then
    fail "private-key material present in $directory"
  fi
  if grep -aEiR '(^|[^[:alnum:]_])(mnemonic|secret_seed|wallet_seed|spending_key|payment_plaintext|rseed)[[:space:]]*[:=]' \
      "$directory" >/dev/null; then
    fail "secret seed or payment plaintext present in $directory"
  fi

  local role commit binary_hash certificate_hash ca_hash manifest_hash log_hash
  local actual_manifest_hash actual_log_hash
  role="$(field "$node_manifest" role)" || fail "invalid node role field"
  [[ "$role" == "$expected_role" ]] || \
    fail "expected role $expected_role, found $role"
  commit="$(field "$host_manifest" commit)" || fail "invalid commit field"
  [[ "$commit" =~ ^[0-9a-f]{40}$ ]] || fail "invalid repository commit"
  binary_hash="$(field "$host_manifest" binary_sha256)" || \
    fail "missing binary hash"
  certificate_hash="$(field "$host_manifest" certificate_sha256)" || \
    fail "missing certificate hash"
  ca_hash="$(field "$host_manifest" ca_certificate_sha256)" || \
    fail "missing CA certificate hash"
  manifest_hash="$(field "$host_manifest" node_manifest_sha256)" || \
    fail "missing node manifest hash"
  log_hash="$(field "$host_manifest" node_log_sha256)" || \
    fail "missing node log hash"
  require_hash "$binary_hash" "binary hash"
  require_hash "$certificate_hash" "certificate hash"
  require_hash "$ca_hash" "CA certificate hash"
  require_hash "$manifest_hash" "node manifest hash"
  require_hash "$log_hash" "node log hash"
  actual_manifest_hash="$(sha256sum "$node_manifest" | awk '{print $1}')"
  actual_log_hash="$(sha256sum "$node_log" | awk '{print $1}')"
  [[ "$manifest_hash" == "$actual_manifest_hash" ]] || \
    fail "node manifest hash mismatch for $expected_role"
  [[ "$log_hash" == "$actual_log_hash" ]] || \
    fail "node log hash mismatch for $expected_role"
  [[ "$(field "$host_manifest" private_keys_included)" == "false" ]] || \
    fail "private-key declaration failed for $expected_role"

  if [[ "$mode" == "relay" ]]; then
    grep -q "^stage7_private_load_evidence=PASS role=$expected_role$" \
      "$host_manifest" || fail "capture gate missing for $expected_role"
    grep -q "^stage7_private_load=PASS role=$expected_role " "$node_log" || \
      fail "node PASS record missing for $expected_role"
  elif [[ "$expected_role" == "receiver" ]]; then
    grep -q '^stage7_block_evidence=PASS$' "$host_manifest" || \
      fail "block capture gate missing for $expected_role"
    grep -q '^stage7_block_receiver=PASS ' "$node_log" || \
      fail "block receiver PASS record missing for $expected_role"
  else
    grep -q '^stage7_restart_evidence=PASS$' "$host_manifest" || \
      fail "restart capture gate missing"
    grep -q '^stage7_offline_restart=PASS ' "$node_log" || \
      fail "offline restart PASS record missing"
  fi
  printf '%s %s\n' "$commit" "$ca_hash"
}

mode="${1:-}"
case "$mode" in
  relay)
    (( $# == 4 )) || {
      echo "usage: $0 relay ORIGIN_DIR RELAY_DIR OBSERVER_DIR" >&2
      exit 2
    }
    roles=(origin relay observer)
    ;;
  block)
    (( $# == 5 )) || {
      echo "usage: $0 block RECEIVER_A_DIR RECEIVER_B_DIR RESTART_A_DIR RESTART_B_DIR" >&2
      exit 2
    }
    roles=(receiver receiver restart restart)
    ;;
  *)
    echo "usage: $0 relay ORIGIN_DIR RELAY_DIR OBSERVER_DIR | block RECEIVER_A_DIR RECEIVER_B_DIR RESTART_A_DIR RESTART_B_DIR" >&2
    exit 2
    ;;
esac
shift

commits=()
ca_hashes=()
node_manifests=()
index=0
for directory in "$@"; do
  read -r commit ca_hash < <(
    check_bundle "$mode" "${roles[$index]}" "$directory")
  commits+=("$commit")
  ca_hashes+=("$ca_hash")
  node_manifests+=("$directory/node-manifest.txt")
  index=$((index + 1))
done

for commit in "${commits[@]}"; do
  [[ "$commit" == "${commits[0]}" ]] || \
    fail "evidence directories use different repository commits"
done
for ca_hash in "${ca_hashes[@]}"; do
  [[ "$ca_hash" == "${ca_hashes[0]}" ]] || \
    fail "evidence directories use different TLS certificate authorities"
done

if [[ "$mode" == "block" ]]; then
  first_receiver="$(field "${node_manifests[0]}" receiver_id)" || \
    fail "invalid first receiver ID"
  second_receiver="$(field "${node_manifests[1]}" receiver_id)" || \
    fail "invalid second receiver ID"
  [[ "$first_receiver" != "$second_receiver" ]] || \
    fail "block evidence repeats one receiver"
fi

bash "$manifest_validator" "$mode" "${node_manifests[@]}"
printf 'stage7_evidence_bundle=PASS mode=%s commit=%s nodes=%s\n' \
  "$mode" "${commits[0]}" "$#"
