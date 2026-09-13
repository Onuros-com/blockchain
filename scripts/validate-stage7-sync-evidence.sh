#!/usr/bin/env bash
set -euo pipefail

if (( $# != 4 )); then
  echo "usage: $0 SERVER INITIAL_CLIENT LATE_CLIENT RESTARTED_CLIENT" >&2
  exit 2
fi

directories=("$@")

field() {
  local file="$1"
  local key="$2"
  local value
  value="$(sed -n "s/^${key}=//p" "$file")"
  [[ -n "$value" && "$(grep -c "^${key}=" "$file")" == 1 ]] || {
    echo "missing or duplicate $key in $file" >&2
    exit 1
  }
  printf '%s' "$value"
}

for directory in "${directories[@]}"; do
  [[ -d "$directory" ]] || { echo "missing evidence: $directory" >&2; exit 1; }
  if find "$directory" -type l -print -quit | grep -q .; then
    echo "symbolic link in evidence: $directory" >&2
    exit 1
  fi
  if find "$directory" -type f -name '*.key' -print -quit | grep -q .; then
    echo "private-key file in evidence: $directory" >&2
    exit 1
  fi
  for file in host-manifest.txt node-manifest.txt node.log; do
    [[ -f "$directory/$file" ]] || {
      echo "missing $file in $directory" >&2
      exit 1
    }
  done
  while IFS= read -r evidence_file; do
    case "$(basename "$evidence_file")" in
      host-manifest.txt|node-manifest.txt|node.log) ;;
      *)
        echo "unexpected file in evidence: $evidence_file" >&2
        exit 1
        ;;
    esac
  done < <(find "$directory" -type f -print)
  if grep -aER 'BEGIN .*PRIVATE KEY' "$directory" >/dev/null; then
    echo "private-key material in evidence: $directory" >&2
    exit 1
  fi
  [[ "$(field "$directory/host-manifest.txt" private_keys_included)" == false ]]
  [[ "$(field "$directory/host-manifest.txt" stage7_sync_evidence)" == PASS ]]
  printf '%s  %s\n' \
    "$(field "$directory/host-manifest.txt" node_manifest_sha256)" \
    "$directory/node-manifest.txt" | sha256sum -c - >/dev/null
  printf '%s  %s\n' \
    "$(field "$directory/host-manifest.txt" node_log_sha256)" \
    "$directory/node.log" | sha256sum -c - >/dev/null
  [[ "$(field "$directory/node-manifest.txt" synchronization)" == PASS ]]
  [[ "$(field "$directory/node-manifest.txt" process_exit_status)" == 0 ]]
  [[ "$(field "$directory/node-manifest.txt" private_payloads_logged)" == false ]]
done

server="${directories[0]}/node-manifest.txt"
initial="${directories[1]}/node-manifest.txt"
late="${directories[2]}/node-manifest.txt"
restart="${directories[3]}/node-manifest.txt"

[[ "$(field "$server" role)" == server && "$(field "$server" mode)" == served ]]
for client in "$initial" "$late"; do
  [[ "$(field "$client" role)" == client && "$(field "$client" mode)" == initial ]]
  [[ "$(field "$client" transport_authenticated)" == true ]]
  [[ "$(field "$client" tls_cipher)" == TLS_* ]]
  (( $(field "$client" useful_bytes) > 0 ))
done
[[ "$(field "$server" transport_authenticated)" == true ]]
[[ "$(field "$server" tls_cipher)" == TLS_* ]]
[[ "$(field "$restart" role)" == client && "$(field "$restart" mode)" == recovered ]]

server_id="$(field "$server" node_id)"
initial_id="$(field "$initial" node_id)"
late_id="$(field "$late" node_id)"
restart_id="$(field "$restart" node_id)"
[[ "$server_id" != "$initial_id" && "$server_id" != "$late_id" && \
   "$initial_id" != "$late_id" && "$restart_id" == "$initial_id" ]]

tip="$(field "$server" tip)"
height="$(field "$server" height)"
[[ "$tip" =~ ^[0-9a-f]{64}$ && "$height" =~ ^[1-9][0-9]*$ ]]
for manifest in "$initial" "$late" "$restart"; do
  [[ "$(field "$manifest" tip)" == "$tip" ]]
  [[ "$(field "$manifest" height)" == "$height" ]]
done

commit="$(field "${directories[0]}/host-manifest.txt" commit)"
ca_hash="$(field "${directories[0]}/host-manifest.txt" ca_certificate_sha256)"
[[ "$commit" =~ ^[0-9a-f]{40}$ && "$ca_hash" =~ ^[0-9a-f]{64}$ ]]
for directory in "${directories[@]:1}"; do
  [[ "$(field "$directory/host-manifest.txt" commit)" == "$commit" ]]
  [[ "$(field "$directory/host-manifest.txt" ca_certificate_sha256)" == "$ca_hash" ]]
done

echo "stage7_published_sync_gate=PASS nodes=3 height=$height tip=$tip"
