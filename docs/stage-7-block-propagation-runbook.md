# Stage 7 near-limit block propagation runbook

> Historical Orchard/Halo2 document. Superseded for active protocol use;
> `active_privacy_protocol_qualified=false`. Retained for traceability.

This procedure closes Stage 7 Gate 7. It transfers the same block from one
sender to two independent receivers. Each receiver reconstructs the compact
block with a measured mempool overlap, verifies every Orchard proof, activates
the block and shielded state, and reopens both databases before reporting
success.

The in-process `stage7_block_limit` test remains a regression check. It is not
physical Gate 7 evidence.

## Topology and controls

Use the public GCP node as the sender. Run one receiver on the RTX 3060
workstation and one on the RTX 4070 workstation. The sender listens and both
workstations make outbound connections, avoiding WSL and residential NAT
inbound forwarding. Restrict the GCP sender firewall to the two workstation
public addresses.

Use certificates issued by the Stage 7 private CA and check each peer DNS name
against its certificate SAN. Never copy the CA key to a workstation or place a
private key in an evidence directory.

All three hosts must run the same immutable commit. Build with:

```bash
git pull --ff-only
bash scripts/build-stage7-private-load.sh
```

## Prepare the block corpus

On GCP, generate 3,000 distinct valid transactions before the timed run:

```bash
mkdir -p "$HOME/onuros-stage7-block"
bash scripts/generate-stage7-private-corpus.sh \
  "$HOME/onuros-stage7-block/orchard-3000.onc" 3000 "$(nproc)"
```

The sender consumes as many entries as fit under the 16 MiB consensus ceiling.
It rejects a block below 99% of that ceiling. Corpus creation and sender-side
block construction are outside the receiver latency interval. Do not commit
the corpus; it contains private test transaction bodies and is reproducible
from the pinned generator.

## Run the RTX 3060 receiver

Create fresh evidence and data directories on the RTX 3060 receiver:

```bash
EVIDENCE="$HOME/onuros-stage7-evidence/block-rtx3060"
DATA="$HOME/onuros-stage7-data/block-rtx3060-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$EVIDENCE"
```

Start the sender on GCP and wait for `READY role=sender`:

```bash
EVIDENCE="$HOME/onuros-stage7-evidence/block-to-rtx3060"
mkdir -p "$EVIDENCE"
./build-stage7-private-load/onuros_stage7_block_propagation_node \
  --role sender --bind 0.0.0.0 --port 39446 \
  --cert "$TLS_DIR/gcp.crt" --key "$TLS_DIR/gcp.key" \
  --ca "$TLS_DIR/ca.crt" --expected-peer RTX3060_CERTIFICATE_DNS \
  --corpus "$HOME/onuros-stage7-block/orchard-3000.onc" \
  --overlap 50 --manifest "$EVIDENCE/sender.manifest" \
  2>&1 | tee "$EVIDENCE/sender.log"
```

Then run on the RTX 3060:

```bash
./build-stage7-private-load/onuros_stage7_block_propagation_node \
  --role receiver --address GCP_PUBLIC_IP --port 39446 \
  --cert "$TLS_DIR/rtx3060.crt" --key "$TLS_DIR/rtx3060.key" \
  --ca "$TLS_DIR/ca.crt" --expected-peer GCP_CERTIFICATE_DNS \
  --receiver-id rtx3060 --data-dir "$DATA" --workers "$(nproc)" \
  --manifest "$EVIDENCE/receiver.manifest" \
  2>&1 | tee "$EVIDENCE/receiver.log"
```

## Run the RTX 4070 receiver

On GCP, repeat the sender command with a new evidence directory and
`--expected-peer RTX4070_CERTIFICATE_DNS`. Use the same corpus and the same
`--overlap 50` value. On the RTX 4070 host run:

```bash
EVIDENCE="$HOME/onuros-stage7-evidence/block-rtx4070"
DATA="$HOME/onuros-stage7-data/block-rtx4070-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$EVIDENCE"
./build-stage7-private-load/onuros_stage7_block_propagation_node \
  --role receiver --address GCP_PUBLIC_IP --port 39446 \
  --cert "$TLS_DIR/rtx4070.crt" --key "$TLS_DIR/rtx4070.key" \
  --ca "$TLS_DIR/ca.crt" --expected-peer GCP_CERTIFICATE_DNS \
  --receiver-id rtx4070 --data-dir "$DATA" --workers "$(nproc)" \
  --manifest "$EVIDENCE/receiver.manifest" \
  2>&1 | tee "$EVIDENCE/receiver.log"
```

The generated block is deterministic for the corpus, so both receiver
manifests must contain the same `block_id`. A data directory is deliberately
single-use; reusing one would weaken the restart assertion and is rejected.

## Capture and validate

On each receiver:

```bash
bash scripts/capture-stage7-block-evidence.sh \
  "$EVIDENCE/final" "$EVIDENCE/receiver.manifest" \
  "$EVIDENCE/receiver.log" \
  ./build-stage7-private-load/onuros_stage7_block_propagation_node \
  "$TLS_DIR/RECEIVER.crt" "$TLS_DIR/ca.crt"
```

Transfer the two `final` directories to the audit host. Confirm that neither
archive contains a `.key` file, then run:

```bash
bash scripts/validate-stage7-evidence-bundle.sh block \
  rtx3060 rtx4070
```

The validator requires two distinct receiver IDs, a common block ID, a block
between 99% and 100% of 16 MiB, nonzero compact-relay announcement/request/
response byte counts, measured mempool overlap, full Orchard validation,
durable activation and restart recovery. Each receiver must finish the timed
propagation, validation, commit and reopen path in at most 30.000 seconds.
It also requires both evidence bundles to name the same immutable commit and
TLS CA, verifies their captured manifest and log hashes, and rejects private-key
files, symbolic links or private-key PEM material.
