# Groth16/Poseidon multi-PC testnet qualification

Status: candidate-linked payment-relay and genesis-sync executables are ready;
the physical three-host runs and their reviewed manifests remain open.

## Required topology

Use three genuinely independent physical hosts on non-loopback addresses:

| Role | Minimum purpose | Suggested baseline |
| --- | --- | --- |
| Origin/producer | Streams the pinned unique-payment corpus | 8 CPU threads, 16 GiB RAM, SSD |
| Relay/validator | Validates then relays every payment | 8 CPU threads, 16 GiB RAM, SSD |
| Late observer/validator | Validates relayed payments; later performs fresh catch-up | 4 CPU threads, 8 GiB RAM, SSD |

Use at least two CPU models. GPUs are not required for Groth16 verification.
Record CPU model, logical cores, RAM, storage, OS/kernel, compiler, Rust
toolchain, network interface, link rate and whether WSL/virtualization is used.

## Immutable inputs

All hosts must use exactly the same Blockchain commit, Privacy Lab commit,
`params.bin` SHA-256, circuit version, `payments.bin` SHA-256 and `ONURCRP1`
header, network ID, root height, initial Poseidon root, and TLS CA SHA-256.
Each host gets only its own certificate/private key and the public CA
certificate. Never copy the CA private key or runtime private keys into evidence.

## Build

```bash
export ONUROS_PRIVACY_LAB_DIR="$HOME/Onuros-privacy-lab"
bash scripts/build-stage7-private-load.sh
```

The build must report `groth16-bls12-381`, `poseidon`, and the linked
`libonuros_privacy_engine_ffi` path. Do not use an archived Orchard script.

## 600-second private relay

Set these identical values on every host:

```bash
PARAMS="$HOME/onuros-candidate/params.bin"
CORPUS="$HOME/onuros-candidate/payments.bin"
INITIAL_COMMITMENTS="$HOME/onuros-candidate/initial-commitments.bin"
PARAMS_SHA="<64 lowercase hex>"
CORPUS_SHA="<64 lowercase hex>"
INITIAL_COMMITMENTS_SHA="<64 lowercase hex>"
ROOT="<64 lowercase hex>"
BLOCKCHAIN_SHA="<40 lowercase hex>"
PRIVACY_LAB_SHA="<40 lowercase hex>"
COMMON="--parameters $PARAMS --parameters-sha256 $PARAMS_SHA --network-id 1 --circuit-version 1 --root-height 100 --root $ROOT --blockchain-commit $BLOCKCHAIN_SHA --privacy-lab-commit $PRIVACY_LAB_SHA"
```

Start the relay first:

```bash
./build-stage7-private-load/onuros_stage7_private_load_node \
  --role relay --bind 0.0.0.0 --port 39447 \
  --cert "$TLS/relay.crt" --key "$TLS/relay.key" --ca "$TLS/ca.crt" \
  --expected-origin onuros-origin --expected-observer onuros-observer \
  --workers 8 --node-id relay-host $COMMON \
  --manifest evidence/relay.manifest 2>&1 | tee evidence/relay.log
```

Start the origin, then the observer:

```bash
./build-stage7-private-load/onuros_stage7_private_load_node \
  --role origin --address RELAY_IP --port 39447 \
  --cert "$TLS/origin.crt" --key "$TLS/origin.key" --ca "$TLS/ca.crt" \
  --expected-relay onuros-relay --corpus "$CORPUS" \
  --duration 600 --rate 110 --batch 16 --workers 8 \
  --node-id origin-host $COMMON \
  --manifest evidence/origin.manifest 2>&1 | tee evidence/origin.log

./build-stage7-private-load/onuros_stage7_private_load_node \
  --role observer --address RELAY_IP --port 39447 \
  --cert "$TLS/observer.crt" --key "$TLS/observer.key" --ca "$TLS/ca.crt" \
  --expected-relay onuros-relay --workers 8 --node-id observer-host $COMMON \
  --manifest evidence/observer.manifest 2>&1 | tee evidence/observer.log
```

Validate the manifests with
`scripts/validate-stage7-performance-evidence.sh relay ...`. It rejects
loopback, repeated node IDs, identity mismatches, fewer than 60,000 admitted
payments, less than 600 seconds, less than 100 payments/s, divergence, missing
queue instrumentation, or a backend other than the Privacy Engine.

## Genesis synchronization and restart

This is a separate gate. Payment-relay manifests deliberately record
`genesis_sync_qualified=false`; do not relabel them. Use
`onuros_stage7_candidate_sync_node`, not the generic
`onuros_stage7_network_node` transport fixture.

On the producer, run the sender once for each late receiver. The complete block
is built and committed before `READY`, so a receiver connecting afterward is a
late catch-up node. Use the same corpus, parameters, commits, CA, overlap, and
producer data on both invocations:

```bash
./build-stage7-private-load/onuros_stage7_candidate_sync_node \
  --role sender --bind 0.0.0.0 --port 39448 --node-id producer-host \
  --data-dir "$HOME/onuros-stage7/producer" \
  --cert "$TLS/producer.crt" --key "$TLS/producer.key" --ca "$TLS/ca.crt" \
  --expected-peer onuros-validator-a --corpus "$CORPUS" \
  --corpus-sha256 "$CORPUS_SHA" --parameters "$PARAMS" \
  --initial-commitments "$INITIAL_COMMITMENTS" \
  --initial-commitments-sha256 "$INITIAL_COMMITMENTS_SHA" \
  --parameters-sha256 "$PARAMS_SHA" --blockchain-commit "$BLOCKCHAIN_SHA" \
  --privacy-lab-commit "$PRIVACY_LAB_SHA" --overlap 50 --workers 8 \
  --manifest evidence/sender-a.manifest
```

On each of the other two physical hosts, after the matching sender prints
`READY`:

```bash
./build-stage7-private-load/onuros_stage7_candidate_sync_node \
  --role receiver --address PRODUCER_IP --port 39448 \
  --node-id validator-a --receiver-id validator-a \
  --data-dir "$HOME/onuros-stage7/validator-a" \
  --cert "$TLS/validator-a.crt" --key "$TLS/validator-a.key" \
  --ca "$TLS/ca.crt" --expected-peer onuros-producer \
  --corpus "$CORPUS" --corpus-sha256 "$CORPUS_SHA" \
  --initial-commitments "$INITIAL_COMMITMENTS" \
  --initial-commitments-sha256 "$INITIAL_COMMITMENTS_SHA" \
  --parameters "$PARAMS" --parameters-sha256 "$PARAMS_SHA" \
  --blockchain-commit "$BLOCKCHAIN_SHA" \
  --privacy-lab-commit "$PRIVACY_LAB_SHA" --workers 8 \
  --manifest evidence/validator-a.manifest
```

After both receivers exit successfully, stop the producer and disconnect its
network. On each receiver host run a new process with no address, port,
certificate, key, or expected-peer argument:

```bash
./build-stage7-private-load/onuros_stage7_candidate_sync_node \
  --role restart --node-id validator-a \
  --data-dir "$HOME/onuros-stage7/validator-a" --ca "$TLS/ca.crt" \
  --corpus "$CORPUS" --corpus-sha256 "$CORPUS_SHA" \
  --initial-commitments "$INITIAL_COMMITMENTS" \
  --initial-commitments-sha256 "$INITIAL_COMMITMENTS_SHA" \
  --parameters "$PARAMS" --parameters-sha256 "$PARAMS_SHA" \
  --blockchain-commit "$BLOCKCHAIN_SHA" \
  --privacy-lab-commit "$PRIVACY_LAB_SHA" \
  --manifest evidence/validator-a-restart.manifest
```

Validate both receiver and both separate-process restart manifests:

```bash
scripts/validate-stage7-performance-evidence.sh block \
  evidence/validator-a.manifest evidence/validator-b.manifest \
  evidence/validator-a-restart.manifest evidence/validator-b-restart.manifest
```

The validator requires the same genesis hash, terminal block ID, initial
candidate root, terminal Poseidon note root, parameter and corpus SHA-256,
initial-commitment snapshot SHA-256, TLS-CA SHA-256, Blockchain commit,
Privacy Lab commit, network ID, circuit
version, and root height. It rejects loopback and requires two separate
no-network restart manifests. Genesis sync remains `OPEN` until those physical
manifests exist and pass. The generic transport fixture remains
`active_privacy_protocol_qualified=false`.

## Reporting contract

Publish raw logs and per-host manifests before summaries. Report transaction
count, exact bytes, duration, admitted rate, verification seconds, p50/p95/p99/
maximum latency, CPU, peak RSS, disk/fsync and network bytes. Name percentile
rules and disclose failures, warm-up, batching and hardware assumptions.
Evidence must contain no private key, symlink, secret seed, or unredacted
payment plaintext.
