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
    roles=""
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
         "$backend" == "orchard-ffi" && "$exit_status" == "0" &&
         "$payloads" == "false" && "$set_hash" =~ ^[0-9a-f]{64}$ ]] || {
        echo "relay invariant failed: $manifest" >&2
        exit 1
      }
      [[ " $roles " != *" $role "* ]] || {
        echo "relay role repeated: $role" >&2
        exit 1
      }
      roles="$roles $role"
      if [[ -z "$expected_set" ]]; then expected_set="$set_hash";
      elif [[ "$set_hash" != "$expected_set" ]]; then
        echo "relay transaction sets diverged" >&2
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
    [[ "$#" -eq 3 ]] || {
      echo "usage: $0 block RECEIVER_A RECEIVER_B" >&2
      exit 2
    }
    shift
    expected_id=""
    receivers=""
    for manifest in "$@"; do
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
      backend="$(field "$manifest" verification_backend)"
      exit_status="$(field "$manifest" process_exit_status)"
      payloads="$(field "$manifest" private_payloads_logged)"
      awk -v b="$bytes" -v e="$elapsed" -v t="$transactions" \
          -v o="$overlap" -v ot="$overlap_transactions" \
          -v a="$announcement" -v q="$request" -v r="$response" '
        BEGIN {
          limit=16777216;
          exit(b >= int(limit * 0.99) && b <= limit && e <= 30.0 &&
               t >= 1800 && o > 0 && o < 100 && ot > 0 &&
               a > 0 && q > 0 && r > 0 ? 0 : 1)
        }
      ' || { echo "block size/latency gate failed: $manifest" >&2; exit 1; }
      [[ "$role" == "receiver" && -n "$receiver" &&
         "$block_id" =~ ^[0-9a-f]{64}$ && "$limits" == "0" &&
         "$validation" == "PASS" && "$durable" == "PASS" &&
         "$restart" == "PASS" && "$backend" == "orchard-ffi" &&
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
    done
    printf 'stage7_block_propagation_gate=PASS receivers=2 maximum_seconds=30 block_id=%s\n' "$expected_id"
    ;;
  *)
    echo "usage: $0 relay NODE_A NODE_B NODE_C | block RECEIVER_A RECEIVER_B" >&2
    exit 2
    ;;
esac
