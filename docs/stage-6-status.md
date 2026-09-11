# Stage 6 status: private transaction admission

Stage 6 is in progress on the `private-transactions` branch.

## Checkpoint 1

The first checkpoint adds a fail-closed boundary between consensus admission and
the future Orchard/Halo2 implementation:

- `PrivateTransactionVerifier` is the only source of authenticated private
  effects.
- Admission rejects malformed/invalid proofs, unknown anchors, repeated
  nullifiers or commitments, duplicate transactions, negative or overflowing
  fees, and configured size/action-limit violations.
- A successful verification produces a `Prepared` capability tied to the
  current shielded tip and root.
- `ShieldedState` atomically connects prepared effects and records an undo
  entry for ordered disconnect/reorganization.
- Unit tests use a clearly named scripted verifier. It is test scaffolding, not
  a production cryptographic verifier.

This preserves the Stage 3 rule that a caller-supplied boolean can never stand
in for proof verification. The production gate remains closed until a pinned,
reviewed Orchard-compatible verifier parses the canonical bundle and returns
authenticated effects.

## Remaining Stage 6 work

1. Define the versioned canonical private transaction bundle and bounded parser.
2. Connect the pinned Orchard/Halo2 verification backend.
3. Commit the shielded root, nullifiers, commitments, and undo records into the
   persistent block/reorganization path.
4. Bind verified private fees and recipients into reward validation.
5. Add mempool conflict handling, multi-node tests, and reproducible private-TPS
   benchmarks.

The Stage 5 local node still uses deterministic CPU test proof of work. Its
current output is useful for block/restart validation, not GPU or private-TPS
measurement.
