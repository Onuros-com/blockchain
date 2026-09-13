# Stage 7 recursive block-proof feasibility gate

This gate prevents estimated proof compression from being reported as a Stage
7 result. It accepts measurements only for exactly 6,000 private transfers and
requires every size, performance and security boundary below to pass.

- canonical effects, proof and block overhead together are at most 16 MiB;
- recursive prover p99 is below 45 seconds;
- CPU verification completes within the 60-second block interval;
- independent-node propagation p99 is below 15 seconds;
- peak proving memory fits the 12 GiB RTX 3060 reference device;
- repeated runs are deterministic;
- the proof binds the parent and exact PoW block, ordered effects and
  authorizations, old and new state roots, nullifiers, values, fees and reward;
- corrupted and withheld proofs fail closed.

Zero-valued timing, memory, effect and proof measurements fail. All size sums
are checked for overflow before comparison with the block ceiling. Boundary
tests treat the time limits as strict: equality fails, preserving operational
headroom.

The evaluator is not a recursive prover and does not activate a proof system.
It is the machine-readable evidence contract the prototype must satisfy before
the compact format or aggregate proof can become consensus work.
