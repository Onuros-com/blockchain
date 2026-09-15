# Stage 6 status: private transaction admission

> Evidence status: superseded historical Orchard/Halo2 checkpoint.
> `active_privacy_protocol_qualified=false`. Raw results are retained unchanged.

Stage 6 implementation and validation are complete on the
`private-transactions` branch. This closes the Stage 6 development milestone;
it is not a claim that the wider node or network is ready for mainnet.

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
- `scripts/run-private-tps.sh` launches synchronized independent processes that
  verify genuine pinned-Orchard Halo2 proofs and reports aggregate sustained
  verification TPS. One shared fixture is generated before workers launch;
  per-process warm-up also happens before the synchronized timed interval. Run
  it as `./scripts/run-private-tps.sh WORKERS ITERATIONS` on the published
  benchmark machine.
- The Orchard verifying key is initialized once per process with `OnceLock` and
  then reused immutably. This removes deterministic key construction from the
  per-transaction hot path while retaining fail-closed panic handling at the
  FFI boundary.

## Performance baseline

The first WSL hardware run successfully verified every real Orchard proof:

- one worker, five verifications: 0.795 aggregate TPS;
- four workers, 400 verifications: 2.137 aggregate TPS in 187.217 seconds.

Those measurements were taken before verifying-key caching and establish the
optimization baseline. They demonstrate correct concurrent verification, but
do not satisfy the 100 sustained private-TPS target. A post-cache run on the
same machine is required for an apples-to-apples comparison.

The same WSL machine was then retested at commit `18953ba` after enabling the
process-lifetime verifying-key cache:

- one worker, 100 verifications: 89.647 aggregate TPS in 1.11548 seconds;
- four workers, 400 verifications: 150.542 aggregate TPS in 2.65706 seconds.

Every proof verified successfully. The four-worker result exceeds the initial
100 sustained private-verification TPS target by 50.5% and improves the
pre-cache four-worker baseline by about 70.4 times. CPU model, logical-core
count and available RAM still need to be captured alongside this result before
the hardware evidence is considered fully published. End-to-end node TPS is a
separate measurement and is not claimed by this verifier benchmark.

The sustained follow-up captured the benchmark hardware and repeated the test
three times:

- AMD Ryzen 5 3600, 6 cores / 12 logical CPUs;
- 7.7 GiB WSL2 memory with 2.0 GiB swap;
- Linux 6.6.114.1-microsoft-standard-WSL2;
- four independent verifier processes, 1,000 verifications each per run;
- 153.242, 153.085 and 153.094 aggregate TPS;
- 153.140 mean aggregate TPS across 12,000 successful verifications.

The run-to-run range was 0.157 TPS (about 0.1% of the mean). This satisfies the
100 sustained real-Orchard private-verification TPS checkpoint on published
hardware. The GPU was not used. Full mempool-to-block node throughput remains
a separate completion measurement.

## Stage 6 completion gates

1. **Passed:** green pinned-Orchard workflow, including the Rust
   proof/signature test and a C++ build linked to the generated library.
2. **Passed:** reproducible multi-process benchmark using real Orchard
   verification on published hardware, exceeding 100 sustained verification
   TPS in three consecutive runs.
3. **Passed:** a genuinely spendable Orchard bundle is signed over the exact
   canonical Onuros transaction digest and exercised through real FFI
   verification, mempool admission, deterministic block selection,
   shielded-root commitment, persistence/restart, disconnect and reconnect.
   The same test rejects altered spend signatures, proofs and shielded roots.
   It runs automatically in the linked Orchard CI suite.

All Stage 6 gates are therefore complete at commit `9b26912`. The published
153.140 TPS figure remains specifically a sustained Orchard verification
measurement. Full multi-node/network throughput and confirmation latency must
still be measured in a later networking/testnet stage.

The Stage 6 block-header/database encoding is intentionally incompatible with
Stage 5 databases. Use a new data path when running this branch.

The Stage 5 local node still uses deterministic CPU test proof of work. Its
current output is useful for block/restart validation, not GPU or private-TPS
measurement.
