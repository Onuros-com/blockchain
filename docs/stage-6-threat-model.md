# Stage 6 private-transaction threat model

> Historical Orchard/Halo2 threat model; it does not describe the active
> Groth16/Poseidon candidate. `active_privacy_protocol_qualified=false`.

Status: completion checkpoint for pre-mainnet development

## Protected assets and trust boundaries

Stage 6 protects consensus supply, nullifier uniqueness, the shielded commitment
root, canonical transaction identity, database recovery, and proof/signature
validity. Transaction bytes, blocks, persisted state, benchmark inputs and—in
Stage 7—network peers are untrusted. The pinned Orchard implementation is a
critical dependency, not an oracle whose return value may bypass Onuros rules.

## Threats and current controls

| Threat | Current control | Remaining risk |
| --- | --- | --- |
| Malformed or ambiguous encoding | Canonical bounded decoder, exact re-encoding tests, trailing-byte rejection and deterministic mutation corpus | A coverage-guided fuzzer and independent protocol review remain advisable |
| Proof or binding-signature bypass | Real Orchard verification is mandatory at node admission; end-to-end tests tamper proof, signature and digest inputs | Cryptographic integration has not received an external audit |
| Replay or double spend | Nullifier uniqueness is enforced across mempool and persisted active-chain state | Stage 7 must preserve atomic behavior under concurrent peer admission |
| Commitment/root divergence | Deterministic commitment updates, persisted roots, restart and reorganization tests | Cross-implementation consensus vectors do not yet exist |
| Inflation or reward abuse | Exact economics and private reward binding are contextually validated | Full monetary-policy review remains required before public testnet |
| Parser/resource exhaustion | Action, proof, transaction and store bounds; deterministic random/mutation test under sanitizers | Peer-level bandwidth, queue and CPU budgets belong to Stage 7 |
| Torn write or corrupt storage | Checksummed append-only block storage and recovery tests; shielded tip reconciliation | Crash injection across every persistence boundary is not yet exhaustive |
| Reorganization inconsistency | Strongest-chain selection and atomic block/shielded-state reorganization tests | Distributed races cannot be assessed until multi-node networking exists |
| Dependency compromise | Orchard revision and Cargo lockfile are pinned; locked builds run in CI | Dependency provenance, release signing and reproducible binaries remain open |
| Key or privacy leakage | No master viewing key; tests use deterministic fixtures; policy forbids secrets and private contents in logs | Timing and transaction-origin leakage require Stage 7 relay analysis |
| Misleading performance claims | Hardware, workload and raw aggregate counts are recorded; verification TPS is separated from network TPS | End-to-end network throughput and confirmation latency remain unmeasured |

## Security invariants

1. No state mutation occurs before canonical decoding and all required local
   cryptographic and contextual checks succeed.
2. A nullifier can be consumed at most once in the active chain and cannot be
   admitted twice to the mempool.
3. Rejected or interrupted work cannot partially advance the shielded root.
4. Restart and reorganization produce the same active tip and shielded root as
   uninterrupted validation of the selected strongest chain.
5. Test-only deterministic proof-of-work and key material cannot be mistaken for
   production security.

## Release decision

These controls are sufficient for the Stage 6 engineering milestone and Stage 7
private-network development. They are not sufficient for custody of real funds,
a public mainnet, or a claim of independent security review.
