# Stage 6 status: private transaction admission

Stage 6 implementation is present on the `private-transactions` branch. The
production gate remains closed until the pinned Orchard CI and a reproducible
multi-process private-TPS run are green and recorded.

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

- envelope version 2 carries an `ONP2` versioned private bundle;
- every action canonically encodes its value commitment, nullifier, randomized
  key, note commitment, ephemeral key, ciphertexts, and spend authorization;
- the bundle carries a signed Orchard value balance separately from its
  non-negative ONUROS fee, plus an anchor, proof-system version, proof, and
  binding signature;
- spend and binding signatures authorize a domain-separated digest of the
  canonical bundle (including fee and proof, excluding only signature bytes),
  avoiding a circular dependency on the final transaction identifier;
- because Stage 6 has no transparent value pool, ordinary private admission
  requires the signed Orchard value balance to equal the transaction fee,
  preventing unmatched negative balance from minting value;
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

## Checkpoints 3–6

- The Rust FFI backend is pinned to the official Orchard repository at commit
  `f2be3a479837df6583110fd44e124d155ec592ee`. It verifies the fixed post-NU6.2
  Halo2 proof, every RedPallas spend authorization, and the binding signature.
  Missing or failed backends reject admission.
- Every 160-byte Stage 6 block header commits the resulting Orchard root, making
  it part of the block ID and proof-of-work preimage. The checksummed shielded
  state file persists nullifiers, commitments, roots, and ordered undo history;
  a reorganization is staged and atomically replaced only after all transitions
  succeed.
- The first transaction is a canonical `ONR1` reward transaction. Its miner,
  team, and ecosystem amounts must exactly match the 50 ONUROS reward schedule
  plus fees authenticated by the private proofs. Fixed recipients and canonical
  zero-value recipients are enforced.
- The bounded private mempool rejects duplicate or conflicting nullifiers and
  commitments, revalidates after a tip change, and selects deterministically by
  fee then arrival order. Tests simulate two nodes reaching the same transaction
  root and shielded effects.
- `onuros_private_tps_benchmark` measures only the C++ admission/mempool path and
  labels its result as non-Orchard TPS. It cannot be used as the published
  private-TPS result.

## Validation still required before the Stage 6 completion claim

1. Green pinned-Orchard workflow, including the Rust proof/signature test and a
   C++ build linked to the generated library.
2. A reproducible multi-process benchmark using real Orchard verification on
   published CPU/GPU hardware. The target remains 100 sustained private TPS;
   admission-only or deterministic-PoW numbers do not satisfy it.

The Stage 6 block-header/database encoding is intentionally incompatible with
Stage 5 databases. Use a new data path when running this branch.

The Stage 5 local node still uses deterministic CPU test proof of work. Its
current output is useful for block/restart validation, not GPU or private-TPS
measurement.
