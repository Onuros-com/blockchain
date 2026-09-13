# Stage 7 recursive block-proof feasibility gate

This gate prevents estimated proof compression from being reported as a Stage
7 result. It accepts measurements only for exactly 6,000 private transfers and
requires every size, performance and security boundary below to pass.

- canonical effects, proof and block overhead together are at most 16 MiB;
- recursive prover p99 is below 45 seconds;
- CPU verification completes within the 60-second block interval;
- independent-node propagation p99 is below 15 seconds;
- peak proving memory fits the 12 GiB RTX 3060 reference device;
- all 6,000 inputs pass the real ONP2 Orchard proof and authorization-signature
  verifier before aggregation;
- repeated runs are deterministic;
- the proof binds the parent and exact PoW block, ordered effects and
  authorizations, old and new state roots, nullifiers, values, fees and reward;
- corrupted and withheld proofs fail closed.
- malformed aggregate proofs fail closed and interrupted proving resumes from
  an authenticated checkpoint.

Zero-valued timing, memory, effect and proof measurements fail. All size sums
are checked for overflow before comparison with the block ceiling. Boundary
tests treat the time limits as strict: equality fails, preserving operational
headroom.

The evaluator is not a recursive prover and does not activate a proof system.
It is the machine-readable evidence contract the prototype must satisfy before
the compact format or aggregate proof can become consensus work. The evaluator
reads `onuros-stage7-recursive-proof-v1` manifests and derives determinism from
two identical proof SHA-256 values; a manifest cannot self-assert that result.
Unknown, duplicate, missing or non-canonical fields fail closed.

## Current feasibility result

The pinned `orchard` integration exposes ordinary Halo 2 proof construction
and verification. It does not expose or implement a circuit that verifies an
Orchard proof inside another proof. Consequently there is currently no
recursive backend capable of producing the `proof_bytes`, prover p99, verifier
time, recovery and adversarial evidence required by this gate.

This is a **closed gate**, not a measured pass. Reusing 6,000 ordinary proof
verifications, hashing their results or signing an effects root would be batch
verification or trusted attestation, not recursive proof composition. Those
substitutes must not be labelled a recursive block proof.

The current 16 MiB Stage 7 physical relay and near-limit propagation gates do
not depend on recursive activation and may proceed using full ONP2 transactions.
The proposed compact/4 MiB format remains disabled.

## Evidence manifest

`onuros-stage7-recursive-proof-gate MANIFEST` requires exactly these fields:

```text
format=onuros-stage7-recursive-proof-v1
backend=
backend_commit=
proof_hash_run_1=
proof_hash_run_2=
raw_log_sha256=
transfers=
verified_onp2_transfers=
effect_payload_bytes=
proof_bytes=
block_overhead_bytes=
prover_p99_ms=
verifier_ms=
propagation_p99_ms=
peak_vram_mib=
binds_parent_and_pow_block=
binds_ordered_effects_and_authorizations=
binds_old_and_new_state_roots=
binds_nullifiers_values_fees_and_reward=
rejects_corrupted_proof=
rejects_withheld_proof=
rejects_malformed_proof=
recovers_after_interruption=
```

Hashes are lowercase 64-character SHA-256 values. Booleans are exactly `true`
or `false`; unsigned integers have no sign or suffix. A non-recursive backend
must identify itself as `none`, which parses successfully but fails the gate as
`missing_recursive_backend`. This makes the blocker machine-readable without
turning absence into success.
