#!/usr/bin/env bash
set -euo pipefail

field() {
  local file="$1" key="$2"
  awk -F= -v key="$key" '
    $1 == key { value=$2; count++ }
    END { if (count != 1 || value == "") exit 1; print value }
  ' "$file"
}

require_file() {
  [[ -f "$1" ]] || { echo "missing manifest: $1" >&2; exit 1; }
}

mode="${1:-}"
case "$mode" in
  relay)
    [[ "$#" -eq 4 ]] || {
      echo "usage: $0 relay NODE_A NODE_B NODE_C" >&2
      exit 2
    }
    shift
    expected_set=""
    expected_candidate=""
    roles=""
    nodes=""
    for manifest in "$@"; do
      require_file "$manifest"
      role="$(field "$manifest" role)"
      duration="$(field "$manifest" duration_seconds)"
      submitted="$(field "$manifest" submitted_unique)"
      admitted="$(field "$manifest" admitted_unique)"
      relayed="$(field "$manifest" relayed_unique)"
      duplicates="$(field "$manifest" duplicate_transactions)"
      invalid="$(field "$manifest" invalid_transactions)"
      queue="$(field "$manifest" queue_high_watermark)"
      queue_limit="$(field "$manifest" queue_limit)"
      verification_batches="$(field "$manifest" verification_batches)"
      verification_tasks="$(field "$manifest" verification_tasks)"
      verification_workers="$(field "$manifest" verification_workers)"
      verification_pool_starts="$(field "$manifest" verification_pool_starts)"
      verification_seconds="$(field "$manifest" verification_seconds)"
      admitted_tps="$(field "$manifest" admitted_tps)"
      divergent="$(field "$manifest" divergent_transactions)"
      overflow="$(field "$manifest" limits_exceeded)"
      backend="$(field "$manifest" verification_backend)"
      proof_system="$(field "$manifest" proof_system)"
      commitment_hash="$(field "$manifest" commitment_hash)"
      payment_bytes="$(field "$manifest" payment_bytes)"
      profile="$(field "$manifest" qualification_profile)"
      active_protocol="$(field "$manifest" active_privacy_protocol_qualified)"
      genesis_sync="$(field "$manifest" genesis_sync_qualified)"
      node_id="$(field "$manifest" node_id)"
      loopback="$(field "$manifest" loopback)"
      authenticated="$(field "$manifest" transport_authenticated)"
      network_id="$(field "$manifest" network_id)"
      circuit_version="$(field "$manifest" circuit_version)"
      root_height="$(field "$manifest" root_height)"
      genesis="$(field "$manifest" genesis)"
      candidate_root="$(field "$manifest" candidate_root)"
      parameters="$(field "$manifest" parameters_sha256)"
      tls_ca="$(field "$manifest" tls_ca_sha256)"
      blockchain_commit="$(field "$manifest" blockchain_commit)"
      privacy_lab_commit="$(field "$manifest" privacy_lab_commit)"
      exit_status="$(field "$manifest" process_exit_status)"
      payloads="$(field "$manifest" private_payloads_logged)"
      set_hash="$(field "$manifest" id_set_sha256)"
      awk -v d="$duration" -v s="$submitted" -v a="$admitted" \
          -v r="$relayed" -v q="$queue" -v ql="$queue_limit" \
          -v vb="$verification_batches" -v vt="$verification_tasks" \
          -v vw="$verification_workers" -v ps="$verification_pool_starts" \
          -v vs="$verification_seconds" -v reported="$admitted_tps" '
        BEGIN {
          measured=a / d;
          delta=reported - measured;
          if (delta < 0) delta=-delta;
          exit(d + 0 >= 600 && s + 0 >= a + 0 && a + 0 >= 60000 &&
               measured >= 100 && r + 0 == a + 0 && q + 0 > 0 &&
               q + 0 <= ql + 0 && ql + 0 > 0 && vb + 0 > 0 &&
               vt + 0 == a + 0 && vw + 0 > 0 && vw + 0 <= ql + 0 &&
               ps + 0 == 1 &&
               vs + 0 > 0 && vs + 0 <= d + 0 && delta < 0.001 ? 0 : 1)
        }
      ' || { echo "relay rate/duration gate failed: $manifest" >&2; exit 1; }
      [[ "$role" =~ ^(origin|relay|observer)$ &&
         "$duplicates" == "0" && "$invalid" == "0" &&
         "$queue" =~ ^[0-9]+$ && "$queue_limit" =~ ^[0-9]+$ &&
         "$verification_batches" =~ ^[0-9]+$ &&
         "$verification_tasks" =~ ^[0-9]+$ &&
         "$verification_workers" =~ ^[0-9]+$ &&
         "$verification_pool_starts" =~ ^[0-9]+$ &&
         "$verification_seconds" =~ ^[0-9]+([.][0-9]+)?$ &&
         "$admitted_tps" =~ ^[0-9]+([.][0-9]+)?$ &&
         "$divergent" == "0" && "$overflow" == "0" &&
         "$backend" == "onuros-privacy-engine-abi-v1" &&
         "$proof_system" == "groth16-bls12-381" &&
         "$commitment_hash" == "poseidon" && "$payment_bytes" == "584" &&
         "$profile" == "groth16-poseidon-payment-relay-v1" &&
         "$active_protocol" == "true" && "$genesis_sync" == "false" &&
         "$loopback" == "false" && "$authenticated" == "true" &&
         "$network_id" =~ ^[1-9][0-9]*$ &&
         "$circuit_version" =~ ^[1-9][0-9]*$ &&
         "$root_height" =~ ^[1-9][0-9]*$ &&
         "$genesis" =~ ^[0-9a-f]{64}$ &&
         "$candidate_root" =~ ^[0-9a-f]{64}$ &&
         "$parameters" =~ ^[0-9a-f]{64}$ &&
         "$tls_ca" =~ ^[0-9a-f]{64}$ &&
         "$blockchain_commit" =~ ^[0-9a-f]{40}$ &&
         "$privacy_lab_commit" =~ ^[0-9a-f]{40}$ &&
         "$exit_status" == "0" &&
         "$payloads" == "false" && "$set_hash" =~ ^[0-9a-f]{64}$ ]] || {
        echo "relay invariant failed: $manifest" >&2
        exit 1
      }
      [[ " $roles " != *" $role "* ]] || {
        echo "relay role repeated: $role" >&2
        exit 1
      }
      roles="$roles $role"
      [[ " $nodes " != *" $node_id "* ]] || {
        echo "relay node repeated: $node_id" >&2
        exit 1
      }
      nodes="$nodes $node_id"
      if [[ -z "$expected_set" ]]; then expected_set="$set_hash";
      elif [[ "$set_hash" != "$expected_set" ]]; then
        echo "relay transaction sets diverged" >&2
        exit 1
      fi
      candidate_identity="$network_id:$circuit_version:$root_height:$genesis:$candidate_root:$parameters:$tls_ca:$blockchain_commit:$privacy_lab_commit"
      if [[ -z "$expected_candidate" ]]; then
        expected_candidate="$candidate_identity"
      elif [[ "$candidate_identity" != "$expected_candidate" ]]; then
        echo "relay candidate identities diverged" >&2
        exit 1
      fi
    done
    [[ " $roles " == *" origin "* && " $roles " == *" relay "* &&
       " $roles " == *" observer "* ]] || {
      echo "relay manifests do not cover origin, relay and observer" >&2
      exit 1
    }
    printf 'stage7_unique_relay_gate=PASS nodes=3 minimum_tps=100 minimum_seconds=600 id_set_sha256=%s\n' "$expected_set"
    ;;
  block)
    [[ "$#" -eq 5 ]] || {
      echo "usage: $0 block RECEIVER_A RECEIVER_B RESTART_A RESTART_B" >&2
      exit 2
    }
    shift
    expected_id=""
    expected_identity=""
    receivers=""
    for manifest in "${@:1:2}"; do
      require_file "$manifest"
      role="$(field "$manifest" role)"
      receiver="$(field "$manifest" receiver_id)"
      bytes="$(field "$manifest" block_bytes)"
      transactions="$(field "$manifest" transactions)"
      overlap="$(field "$manifest" mempool_overlap_percent)"
      overlap_transactions="$(field "$manifest" mempool_overlap_transactions)"
      announcement="$(field "$manifest" announcement_bytes)"
      request="$(field "$manifest" request_bytes)"
      response="$(field "$manifest" response_bytes)"
      elapsed="$(field "$manifest" propagation_validation_seconds)"
      block_id="$(field "$manifest" block_id)"
      limits="$(field "$manifest" limits_exceeded)"
      validation="$(field "$manifest" validation)"
      durable="$(field "$manifest" durable_activation)"
      restart="$(field "$manifest" restart_recovery)"
      late="$(field "$manifest" late_catch_up)"
      offline="$(field "$manifest" offline_restart)"
      backend="$(field "$manifest" verification_backend)"
      witness="$(field "$manifest" tracked_witness_backend)"
      payment_bytes="$(field "$manifest" payment_bytes)"
      proof_system="$(field "$manifest" proof_system)"
      commitment_hash="$(field "$manifest" commitment_hash)"
      profile="$(field "$manifest" qualification_profile)"
      active="$(field "$manifest" active_privacy_protocol_qualified)"
      genesis_sync="$(field "$manifest" genesis_sync_qualified)"
      loopback="$(field "$manifest" loopback)"
      authenticated="$(field "$manifest" transport_authenticated)"
      genesis="$(field "$manifest" genesis)"
      candidate_root="$(field "$manifest" candidate_root)"
      terminal_root="$(field "$manifest" terminal_note_root)"
      parameters="$(field "$manifest" parameters_sha256)"
      corpus="$(field "$manifest" corpus_sha256)"
      ca="$(field "$manifest" tls_ca_sha256)"
      blockchain="$(field "$manifest" blockchain_commit)"
      privacy_lab="$(field "$manifest" privacy_lab_commit)"
      network="$(field "$manifest" network_id)"
      circuit="$(field "$manifest" circuit_version)"
      root_height="$(field "$manifest" root_height)"
      exit_status="$(field "$manifest" process_exit_status)"
      payloads="$(field "$manifest" private_payloads_logged)"
      awk -v b="$bytes" -v e="$elapsed" -v t="$transactions" \
          -v o="$overlap" -v ot="$overlap_transactions" \
          -v a="$announcement" -v q="$request" -v r="$response" '
        BEGIN {
          limit=16777216;
          exit(b > 3504000 && b <= limit && e <= 30.0 &&
               t == 6001 && o > 0 && o < 100 && ot > 0 &&
               a > 0 && q > 0 && r > 0 ? 0 : 1)
        }
      ' || { echo "block size/latency gate failed: $manifest" >&2; exit 1; }
      [[ "$role" == "receiver" && -n "$receiver" &&
         "$block_id" =~ ^[0-9a-f]{64}$ && "$limits" == "0" &&
         "$validation" == "PASS" && "$durable" == "PASS" &&
         "$late" == "PASS" && "$restart" == "PASS" &&
         "$offline" == "PENDING_SEPARATE_PROCESS" &&
         "$payment_bytes" == "584" &&
         "$proof_system" == "groth16-bls12-381" &&
         "$commitment_hash" == "poseidon" &&
         "$profile" == "groth16-poseidon-genesis-sync-v1" &&
         "$active" == "true" && "$genesis_sync" == "true" &&
         "$loopback" == "false" && "$authenticated" == "true" &&
         "$backend" == "onuros-privacy-engine-abi-v1" &&
         "$witness" == "onuros-privacy-engine-abi-v2" &&
         "$genesis" =~ ^[0-9a-f]{64}$ &&
         "$candidate_root" =~ ^[0-9a-f]{64}$ &&
         "$terminal_root" =~ ^[0-9a-f]{64}$ &&
         "$parameters" =~ ^[0-9a-f]{64}$ &&
         "$corpus" =~ ^[0-9a-f]{64}$ &&
         "$ca" =~ ^[0-9a-f]{64}$ &&
         "$blockchain" =~ ^[0-9a-f]{40}$ &&
         "$privacy_lab" =~ ^[0-9a-f]{40}$ &&
         "$exit_status" == "0" && "$payloads" == "false" ]] || {
        echo "block invariant failed: $manifest" >&2
        exit 1
      }
      [[ " $receivers " != *" $receiver "* ]] || {
        echo "block receiver repeated: $receiver" >&2
        exit 1
      }
      receivers="$receivers $receiver"
      if [[ -z "$expected_id" ]]; then expected_id="$block_id";
      elif [[ "$block_id" != "$expected_id" ]]; then
        echo "receiver block IDs diverged" >&2
        exit 1
      fi
      identity="$genesis:$candidate_root:$terminal_root:$parameters:$corpus:$ca:$blockchain:$privacy_lab:$network:$circuit:$root_height"
      if [[ -z "$expected_identity" ]]; then expected_identity="$identity";
      elif [[ "$identity" != "$expected_identity" ]]; then
        echo "receiver candidate/genesis identities diverged" >&2
        exit 1
      fi
    done
    restarts=""
    for manifest in "${@:3:2}"; do
      require_file "$manifest"
      role="$(field "$manifest" role)"
      node="$(field "$manifest" node_id)"
      offline="$(field "$manifest" offline_restart)"
      network_attempted="$(field "$manifest" network_attempted)"
      restart_tip="$(field "$manifest" tip)"
      exit_status="$(field "$manifest" process_exit_status)"
      genesis="$(field "$manifest" genesis)"
      candidate_root="$(field "$manifest" candidate_root)"
      terminal_root="$(field "$manifest" terminal_note_root)"
      parameters="$(field "$manifest" parameters_sha256)"
      corpus="$(field "$manifest" corpus_sha256)"
      ca="$(field "$manifest" tls_ca_sha256)"
      blockchain="$(field "$manifest" blockchain_commit)"
      privacy_lab="$(field "$manifest" privacy_lab_commit)"
      network="$(field "$manifest" network_id)"
      circuit="$(field "$manifest" circuit_version)"
      root_height="$(field "$manifest" root_height)"
      identity="$genesis:$candidate_root:$terminal_root:$parameters:$corpus:$ca:$blockchain:$privacy_lab:$network:$circuit:$root_height"
      [[ "$role" == "restart" && -n "$node" &&
         "$offline" == "PASS" && "$network_attempted" == "false" &&
         "$restart_tip" == "$expected_id" && "$exit_status" == "0" &&
         "$identity" == "$expected_identity" ]] || {
        echo "offline restart invariant failed: $manifest" >&2
        exit 1
      }
      [[ " $restarts " != *" $node "* ]] || {
        echo "offline restart node repeated: $node" >&2
        exit 1
      }
      restarts="$restarts $node"
    done
    printf 'stage7_candidate_sync_gate=PASS receivers=2 offline_restarts=2 maximum_seconds=30 block_id=%s\n' "$expected_id"
    ;;
  *)
    echo "usage: $0 relay NODE_A NODE_B NODE_C | block RECEIVER_A RECEIVER_B RESTART_A RESTART_B" >&2
    exit 2
    ;;
esac
