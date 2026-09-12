# Stage 7 performance qualification

## Measurement rules

Stage 7 reports three different rates. They are not interchangeable.

| Measurement | Unit | What it proves |
|---|---|---|
| Orchard verifier capacity | proof verifications/s | Cryptographic verifier throughput for the supplied fixture |
| Unique transaction relay | admitted unique transactions/s | End-to-end admission and relay capacity across three nodes |
| Settlement | transactions/s in activated blocks | Consensus throughput under the selected block size and interval |

The recorded 120.218 verifications/s result closes only the first row. It reused
one valid proof fixture, so it does not establish unique nullifiers, mempool
growth, relay, or settlement.

## Gate 6: sustained unique private relay

### Topology

Run three published nodes on independent machines in at least two network
locations. Use the production Orchard admission composition and mutually
authenticated TLS. Each node must use an isolated database and its own
certificate identity.

### Workload

Submit at least 100 distinct, valid private transactions per second for 600
continuous seconds. The minimum accepted workload is therefore 60,000
transactions. Every transaction must have a distinct canonical transaction ID
and must pass the pinned Orchard verifier. Replaying one fixture, modifying
bytes after proof creation, or substituting the deterministic test verifier
invalidates the run.

Generate proofs before the timed interval unless proof construction is
explicitly part of the measured client workload. Record which choice was used.
Do not commit transaction bodies, keys, notes, witnesses, or plaintext wallet
metadata as evidence.

### Required counters

Each node must emit these aggregate fields at the end of the same UTC interval:

```text
duration_seconds=
submitted_unique=
admitted_unique=
relayed_unique=
duplicate_transactions=
invalid_transactions=
queue_high_watermark=
divergent_transactions=
```

The sender additionally records request latency percentiles and rejected
submissions. Observers record the first and last accepted transaction IDs as
hashes only.

### Pass conditions

- measured duration is at least 600 seconds;
- sender admission rate is at least 100.000 unique valid transactions/s;
- all three nodes contain the same admitted transaction-ID set;
- `divergent_transactions=0`;
- no verifier, relay, or network queue exceeds its configured hard limit;
- no process exits, restarts, or disables verification during the interval;
- logs contain no private transaction bodies.

A run below the rate floor remains useful capacity evidence but does not pass
Gate 6.

## Gate 7: near-limit block propagation

### Payload

Construct a valid block between 99% and 100% of the 16,777,216-byte consensus
ceiling. The block must use the normal canonical encoder and ordinary
transaction-root, proof, state, contextual, proof-of-work, and persistent
activation paths. Synthetic version-1 bodies or a proof-of-work callback that
always returns true are allowed only in the CI reconstruction test, not in
published-node evidence.

### Topology and timing

Announce the block from one published node to two independent peers with the
same TLS and source restrictions used by Gate 6. Both receivers start with the
documented mempool overlap. Measure from receipt of the first valid block
announcement to durable activation after full validation.

The initial private-testnet budget is 30.000 seconds at both receivers. Report
the raw values and p50/p95 only when multiple blocks are tested. Confirmation
latency is a separate measurement and must not be folded into propagation
latency.

### Pass conditions

- encoded size is within the 99%--100% window;
- both receivers reconstruct the same block ID;
- both independently validate and durably activate it;
- neither receiver requests or buffers more than its configured bounds;
- elapsed propagation plus validation is no more than 30.000 seconds per
  receiver;
- restart after activation restores the same active tip and shielded root.

## Automated supporting gates

`onuros_stage7_bandwidth_benchmark` builds a block above 99% of the 16 MiB ceiling, transfers all
missing transactions through bounded compact-block chunks, reconstructs the
exact encoding, and runs full structural and contextual validation. CI records
this as `stage7_block_limit`. It is an in-process safety and regression gate,
not published-node latency evidence.

`run-private-duration-gate.sh` exercises the real pinned Orchard verifier for
a configured duration and rate floor. It remains the cryptographic capacity
gate.

`run-stage7-private-relay.sh` proves three-process framing, admission
ownership, and convergence with a deterministic verifier. It does not satisfy
the distinct-real-proof requirement.

## Evidence manifest

For both physical gates, preserve:

- UTC start and end timestamps;
- OS, CPU, memory, network location, and node role;
- repository commit and dependency lock hashes;
- executable and log SHA-256 values;
- TLS certificate fingerprints, never private keys;
- configured queue, frame, transaction, block, and timeout limits;
- raw per-node counters and final pass/fail calculation.

The final evidence index must link the immutable commit containing these files.
