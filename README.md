# Onuros Local Blockchain Core

This repository contains the C++17 implementation of the Onuros local blockchain core.

Stages 5 and 6 are completed development milestones. Stage 5 was developed on
`local-blockchain`; Stage 6 was developed on `private-transactions`. New Stage 7
work should use focused branches created from the protected `main` checkpoint.

Confirmed economics:

- 50 ONUROS initial block subsidy
- 60-second target block interval
- Halving every 2,102,400 blocks
- Permanent 0.5 ONUROS subsidy floor
- 10% project team/ecosystem allocation
- 60-block reward maturity
- No premine

Stage 5 includes canonical block and transaction encoding, bounded decoding,
contextual validation, exact compact-target and chain-work arithmetic, 60-block
difficulty adjustment, median-time rules, a checksummed append-only database
with torn-write recovery, strongest-chain selection, atomic reorganization
state, and an integrated local validation/mining/restart pipeline.

The active private-payment candidate is Onuros Shielded Payment v1: a fixed
584-byte, one-input/two-output payment using Groth16/BLS12-381 and Poseidon,
accessed through Privacy Engine payment ABI v1 and tracked-witness ABI v2.
Orchard/Halo2 is no longer built, run, or selected by default. Its Stage 6 and
Stage 7 measurements remain in place as explicitly superseded historical
evidence and do not qualify the active protocol.

This remains a pre-mainnet candidate. Physical three-host qualification,
independent circuit review, a frozen parameter identity, and a public
multi-party Groth16 setup remain release gates.

Run `make test` or use CMake/CTest. Build the runnable local node with `make node`,
then follow [the WSL local-node guide](docs/local-node-run.md). Linux, Windows and
sanitizer builds run in GitHub Actions.

Read the [active migration contract](docs/onuros-privacy-engine-migration.md),
[Groth16 decision rationale](docs/decisions/why-groth16-not-orchard.md), and
[multi-PC qualification runbook](docs/onuros-private-testnet-runbook.md).
The Stage 6 completion report, Stage 6 threat model and earlier Stage 7
networking specification are retained historical Orchard/Halo2 records. Stage 4's
hardware and GPU-to-node qualification evidence is tracked separately in the
[validation record](docs/stage-4-deferred-validation.md). That completed
prototype gate does not by itself make the public mining transport or desktop
miner production-ready.

The Stage 4 recovery work adds a pinned KawPoW 0.9.4 CPU consensus verifier,
published vectors, invalid-mix rejection in the node path, and an
[AMD qualification runbook](docs/stage-4-amd-runbook.md). The MI300X ROCm/OpenCL and RTX 3060 CUDA workers both submitted physical GPU
candidates that the Onuros CPU verifier independently accepted after rejecting
a deliberately invalid probe. The hashed raw evidence is preserved in the
separate [Onuros-miner repository](https://github.com/Onuros-com/Onuros-miner).
See the [Stage 4 audit record](docs/stage-4-audit.md) for the exact completed
scope and later production hardening.

The first Stage 7 mining-endpoint checkpoint now defines bounded, versioned
KawPoW job/solution/result messages and a loopback TCP service. It retains the
full block template inside the node, rejects stale or replayed jobs, and admits
only solutions recomputed by the CPU verifier. It can keep one job active after
an invalid qualification probe and subsequently accept a valid GPU submission.
Physical AMD/NVIDIA evidence is complete. Integrating this boundary with the
full node, then adding authenticated public binding and submission rate limits,
remain later Stage 7 checkpoints.

Stage 7 also includes the agreed
[scalability and storage hardening extension](docs/stage-7-scalability-hardening.md):
transaction byte accounting, compact batching, conditional pruning,
authenticated snapshots and a measured sustainable block-production policy.
This work preserves the original single Layer-1 roadmap.

This remains pre-mainnet software. Passing tests and measured throughput do not
constitute a security audit or readiness for real funds.
