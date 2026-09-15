# Stage 7 private relay load runbook

> Historical Orchard/Halo2 document. Superseded for active protocol use;
> `active_privacy_protocol_qualified=false`. Retained for traceability.

This procedure measures unique private-transaction admission and relay across
three independent hosts. It is the physical procedure for Stage 7 Gate 6. The
short loopback test is a build check and does not satisfy the gate.

## Topology

| Role | Initial host | Network direction |
|---|---|---|
| Origin | RTX 3060 workstation | Connects to the relay |
| Relay | GCP `onuros-stage7-node-us` | Listens on one source-restricted TLS port |
| Observer | RTX 4070 workstation | Connects to the relay |

The relay firewall must allow the two workstation public addresses only. Do
not open the load port to `0.0.0.0/0`. Each TLS certificate must have the
expected peer DNS name in its SAN. Keep the CA private key off the
workstations and out of the evidence directory.

## Build

Check out the same immutable commit on all three hosts, then run:

```bash
git pull --ff-only
bash scripts/build-stage7-private-load.sh
```

The build script compiles the pinned Orchard FFI and links
`onuros_stage7_private_load_node` against it. Record the commit and binary hash
before starting a physical run.

## Generate the workload

Generate the corpus on the origin before opening the measurement interval:

```bash
mkdir -p "$HOME/onuros-stage7-private-load"
bash scripts/generate-stage7-private-corpus.sh \
  "$HOME/onuros-stage7-private-load/orchard-66000.onc" 66000 "$(nproc)"
```

The generator constructs 66,000 independently proved Orchard spends. Every
spend has a distinct nullifier and output commitment, and every witness roots
to one common anchor. The generator signs the canonical Onuros transaction
digest after proof creation. Generation time is excluded from relay timing.

The corpus is a private-test fixture, not repository content. Do not commit it
or leave it on the qualification relay and observer. Its adjacent manifest
records its hash, generator hash, transaction count and source commit.

Generation is resumable. The standard command writes an atomic part for each
worker and reuses every complete part after an interruption. A part with a
wrong header, anchor, range, record count or trailing data stops the run; it is
never silently replaced. Parts are removed only after the final corpus has
been assembled and synced.

For preparation across several trusted hosts, divide one fixed shard set into
non-overlapping half-open ranges. This example assigns four of twelve shards
to one host:

```bash
bash scripts/generate-stage7-private-corpus-shards.sh generate \
  "$HOME/orchard-66000.onc" 66000 12 0 4
```

Use `4 8` and `8 12` on the other preparation hosts. Transfer the resulting
`.part-NNN` files and their range manifests to the origin under the same corpus
basename, verify the recorded SHA-256 values, then assemble them:

```bash
bash scripts/generate-stage7-private-corpus-shards.sh merge \
  "$HOME/onuros-stage7-private-load/orchard-66000.onc" 66000 12
```

All shards bind the total count, shard range and common Orchard anchor. The
merge fails unless all ranges are present, contiguous and bound to that
deterministic anchor. Shards contain private transaction bodies but no spending
keys. Do not publish them or include them in evidence. A preparation host that
will later act as relay or observer must remove its shard copies before the
qualification run.

## Preflight

Run the same topology for 60 seconds at 110 transactions/s before the
qualification interval. Use 6,600 corpus entries and preserve the preflight
logs separately. Increase `--workers` only after measuring the host; using more
workers than available CPU threads can reduce throughput.

The order is significant:

1. Start the relay and wait for `READY role=relay`.
2. Start the origin. It connects and waits for the observer.
3. Start the observer.

The relay accepts the origin first and the observer second. The timed interval
does not start until both authenticated peers are connected.

## Qualification commands

Set these paths and names from the issued certificates. The example port is
`39445`.

Relay:

```bash
./build-stage7-private-load/onuros_stage7_private_load_node \
  --role relay --bind 0.0.0.0 --port 39445 \
  --cert "$TLS_DIR/relay.crt" --key "$TLS_DIR/relay.key" \
  --ca "$TLS_DIR/ca.crt" \
  --expected-origin ORIGIN_CERTIFICATE_DNS \
  --expected-observer OBSERVER_CERTIFICATE_DNS \
  --workers 8 --manifest "$EVIDENCE/relay.manifest" \
  2>&1 | tee "$EVIDENCE/relay.log"
```

Origin:

```bash
./build-stage7-private-load/onuros_stage7_private_load_node \
  --role origin --address RELAY_PUBLIC_IP --port 39445 \
  --cert "$TLS_DIR/origin.crt" --key "$TLS_DIR/origin.key" \
  --ca "$TLS_DIR/ca.crt" --expected-relay RELAY_CERTIFICATE_DNS \
  --corpus "$HOME/onuros-stage7-private-load/orchard-66000.onc" \
  --duration 600 --rate 110 --batch 16 --workers 8 \
  --manifest "$EVIDENCE/origin.manifest" \
  2>&1 | tee "$EVIDENCE/origin.log"
```

Observer:

```bash
./build-stage7-private-load/onuros_stage7_private_load_node \
  --role observer --address RELAY_PUBLIC_IP --port 39445 \
  --cert "$TLS_DIR/observer.crt" --key "$TLS_DIR/observer.key" \
  --ca "$TLS_DIR/ca.crt" --expected-relay RELAY_CERTIFICATE_DNS \
  --workers 8 --manifest "$EVIDENCE/observer.manifest" \
  2>&1 | tee "$EVIDENCE/observer.log"
```

The origin paces 66,000 transactions at 110/s. Relay and observer processing
overlap through the streaming connection. Each node verifies every proof in
parallel on a bounded persistent worker pool, then applies nullifier,
commitment, capacity and relay-pool checks in canonical order. TCP
backpressure slows the origin if any downstream node falls behind; the final
measured rate therefore remains end-to-end. The node manifest records the
worker count, pool-start count, verification time, batch count and real queue
high-watermark used for the run.

## Capture and validation

On each host, capture the corresponding manifest and log:

```bash
bash scripts/capture-stage7-private-load-evidence.sh \
  "$OUTPUT" ROLE "$EVIDENCE/ROLE.manifest" "$EVIDENCE/ROLE.log" \
  "$EVIDENCE/ROLE.resource.log" \
  ./build-stage7-private-load/onuros_stage7_private_load_node \
  "$TLS_DIR/ROLE.crt" "$TLS_DIR/ca.crt"
```

Transfer the three evidence directories without any `.key` file. On the audit
host run:

```bash
bash scripts/validate-stage7-evidence-bundle.sh relay \
  origin relay observer
```

A passing result requires at least 600 seconds, at least 100 admitted unique
transactions/s on every node, no duplicate or invalid transaction, no limit
failure, no process failure, and the same transaction-ID set hash on all three
hosts. The bundle validator also requires one immutable commit and one TLS CA
across all hosts, checks the captured manifest and log hashes, and rejects
private-key files, symbolic links or private-key PEM material. A slower result
is capacity evidence but does not close Gate 6.
