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

## Checkpoint 2

The second checkpoint defines the consensus-facing private transaction container:

- envelope version 2 carries an `ONP1` versioned private bundle;
- every action canonically encodes its value commitment, nullifier, randomized
  key, note commitment, ephemeral key, ciphertexts, and spend authorization;
- the bundle commits an anchor, non-negative fee, proof-system version, proof,
  and binding signature;
- the decoder applies body, action, ciphertext, and proof limits before
  allocation and rejects truncation, trailing bytes, empty fields, and unknown
  versions;
- `CanonicalPrivateTransactionVerifier` calls a cryptographic backend only
  after strict decoding, then rejects any backend result whose authenticated
  nullifiers, commitments, anchor, or fee differ from the encoded bundle.

The backend remains an interface: this checkpoint does not claim that placeholder
bytes are valid Orchard proofs. It creates the fail-closed integration point for
the pinned Orchard/Halo2 verifier in checkpoint 3.

## Hardware validation

The Stage 5 database was validated on WSL by mining 20 blocks, restarting the
process, and extending the same database to 40 blocks at active height 39. On
the Stage 6 branch, all 10 checkpoint-1 tests passed under Ubuntu 26.04 with GCC
15.2. This validates persistence/restart and the private-admission boundary on
the user's machine; it is not yet a GPU or private-TPS result.

## Remaining Stage 6 work

1. Connect the pinned Orchard/Halo2 verification backend.
2. Commit the shielded root, nullifiers, commitments, and undo records into the
   persistent block/reorganization path.
3. Bind verified private fees and recipients into reward validation.
4. Add mempool conflict handling, multi-node tests, and reproducible private-TPS
   benchmarks.

The Stage 5 local node still uses deterministic CPU test proof of work. Its
current output is useful for block/restart validation, not GPU or private-TPS
measurement.
