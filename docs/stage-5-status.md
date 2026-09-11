# Stage 5 local blockchain core

Status: in progress.

Stage 4 remains provisionally passed except for the required physical AMD/OpenCL
execution evidence. That external hardware checkpoint is not waived and Stage 4
must not be described as complete until the evidence is recorded.

## Checkpoint 1: canonical block foundation

This checkpoint introduces:

- a fixed-width 256-bit hash type and independently testable SHA-256 primitive;
- canonical little-endian transaction-envelope and block-header encodings;
- deterministic transaction identifiers, Merkle roots and block identifiers;
- explicit header fields for height, parent, timestamp, compact target, nonce and
  the KawPoW mix hash; and
- mutation tests proving that every consensus header field affects the block ID.

The transaction body is deliberately opaque here. Stage 6 remains responsible
for specifying and validating private transaction semantics. The block ID is an
identity hash; KawPoW work verification remains a separate consensus check.

## Remaining Stage 5 work

Checkpoint 2 adds strict bounded decoding and contextual validation for versions,
encoded sizes, transaction roots, height, parent, timestamps, expected target and
an injected CPU proof-of-work verifier. Limits remain explicit inputs so benchmark
evidence—not an accidental hard-coded guess—can determine the eventual block size.

Checkpoint 3 adds overflow-checked 256-bit accumulated work, a fork-aware block
index, strongest-chain selection and explicit ordered disconnect/connect plans.
Equal-work branches do not cause unnecessary tip changes.

Checkpoint 4 adds canonical compact-target conversion, proof-of-work limit checks,
big-endian hash/target comparison, a deterministic 60-block retarget using the
confirmed 60-second block interval, a four-times adjustment clamp, and median-time
calculation over the newest 11 ancestors. The retarget parameters remain explicit
and invalid or non-canonical targets fail closed.

1. Persistent block/index database with atomic restart recovery.
2. Atomic undo/application of reorganization plans.
3. Local mining/validation/restart integration and invalid-block test suite.

## Throughput direction

The first published goal remains 100 sustained private TPS across multiple nodes.
At the confirmed 60-second block target this requires capacity for approximately
6,000 private transactions per block. Stage 5 therefore uses bounded preallocation,
single-pass decoding and explicit resource limits. Later checkpoints will add
parallelizable transaction/proof validation, cached immutable hashes, batched
database writes and incremental state commitments. The 60-second target does not
need to be shortened merely to increase transaction throughput.
