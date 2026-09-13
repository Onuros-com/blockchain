# Stage 7 scalability and storage hardening

This work is an extension of Stage 7, not a replacement for the original
single Layer-1 roadmap. The 100 sustained private TPS objective remains. Relay,
verification and settlement rates must be measured separately, and no rate is
accepted by shifting unbounded storage cost to full nodes.

The current distinct-proof corpus is retained as benchmark input. Its measured
serialized size is also an input to the storage design; corpus generation is
not itself a throughput result.

## Work sequence

### 1. Transaction byte accounting and canonical encoding

Add deterministic accounting for the fixed header, Orchard actions, proof,
authorization signatures, ciphertext and outer framing. Remove only redundant
Onuros encoding. Orchard privacy fields are not shortened or omitted without a
separate cryptographic review and attack tests.

The canonical version-2 baseline and its reduction boundary are recorded in
[Stage 7 private transaction byte accounting](stage-7-private-byte-accounting.md).

Exit evidence:

- a byte-for-byte layout report for representative one- and two-action
  transactions;
- canonical encoding vectors on Linux and Windows;
- rejection of ambiguous, non-canonical and truncated encodings;
- a measured before/after size result.

### 2. Compact batching and parallel verification

Keep individual consensus verification while reducing transport and scheduling
overhead. Inventory announcements, missing-index requests and bounded payload
chunks remain non-consensus hints. Batch failure must identify the same first
invalid transaction as sequential verification.

Exit evidence:

- identical sequential and parallel admission results;
- bounded memory, queue and chunk counts under slow-peer tests;
- measured relay, verifier and end-to-end admission rates;
- no transaction body or private note data in operational logs.

### 3. Conditional safe pruning

Archive nodes retain complete history. A verifier node may discard finalized
historical bodies only after retaining the headers, cumulative-work chain,
current commitment and nullifier state, required reorganization window and the
authenticated recovery material needed to continue independent validation.
Pruning never authorizes deletion of data that is still required for a valid
reorganization.

The disabled-by-default horizon and chain-bound metadata format are specified
in [Stage 7 conditional pruning](stage-7-pruning.md). Body deletion remains
disabled at node level until the recovery checkpoints listed there are
complete.

Exit evidence:

- archive and pruned nodes converge on the same active tip and shielded root;
- restart and permitted reorganization tests pass after pruning;
- corrupted or incomplete prune metadata fails closed;
- retained disk usage and archive availability are measured.

### 4. Authenticated state snapshots

A snapshot is bound to its height, block identifier, cumulative work, consensus
parameters, shielded-state root and content hash. Import verifies that binding
before replacing local state. A snapshot is a synchronization optimization,
not a trusted substitute for consensus. Trustless late-node recovery requires
an authenticated checkpoint or proof chain defined by the protocol.

The deterministic manifest and validate-before-replace import path are
specified in [Stage 7 authenticated shielded-state snapshots](stage-7-authenticated-snapshots.md).
Signer selection and activation remain outside this checkpoint.

Exit evidence:

- deterministic export and import;
- corruption, wrong-chain, rollback and mismatched-root rejection;
- an independent late node reaches the same tip and root;
- snapshot creation and import resource bounds are published.

### 5. Sustainable block-production policy

The 16 MiB value remains an absolute decoding, allocation and denial-of-service
safety bound during Stage 7. It is not a normal block-size target and does not
authorize sustained 16 MiB production. Production and testnet activation limits
must be selected from measured transaction size, annual archival growth,
propagation time, reorganization memory and pruned-node storage.

The selected policy must include a sustained rolling budget and a separately
bounded burst allowance. A miner cannot repeatedly use the absolute decoder
ceiling. Any consensus activation parameter is documented and reviewed before
it is enabled.

The disabled rolling-budget model and its activation blocker are documented in
[Stage 7 block-production budget](stage-7-block-production-policy.md).

Exit evidence:

- maximum annual archival growth at the sustained budget;
- full and pruned-node disk projections;
- propagation and validation at both normal and burst sizes;
- deterministic rolling-window and boundary tests;
- explicit testnet activation parameters.

## Deferred research

Recursive or block-level proof aggregation remains a research track because it
changes the proof and consensus boundary. It may be activated only after a
security design, independent review and adversarial tests. A separate execution
layer is not part of the current roadmap; Onuros remains a single Layer-1
design.

## Change control and reminders

Before starting each item above, the engineering update identifies it as a
`SCALABILITY UPGRADE`, states the expected chain benefit and lists its exit
tests. Any proposal that changes authorization semantics, proof validity,
history requirements or consensus activation is marked `ROADMAP BLOCKER` and
pauses for explicit agreement. Routine implementation inside an agreed item
does not renumber or replace an original roadmap stage.
