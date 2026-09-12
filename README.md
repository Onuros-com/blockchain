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

Stage 6 adds the mandatory private-transaction body, pinned Orchard verification
boundary, shielded state commitment and persistence, private reward binding,
bounded mempool, and a real Orchard end-to-end node pipeline. On a published
Ryzen 5 3600 / WSL2 baseline, four verification workers sustained an average of
153.140 real Orchard verifications per second across three 4,000-transaction
runs. This is cryptographic verification throughput—not yet multi-node network
TPS or end-user confirmation throughput.

Run `make test` or use CMake/CTest. Build the runnable local node with `make node`,
then follow [the WSL local-node guide](docs/local-node-run.md). Linux, Windows and
sanitizer builds run in GitHub Actions.

Read the [Stage 6 completion report](docs/stage-6-completion-report.md),
[Stage 6 threat model](docs/stage-6-threat-model.md), and
[Stage 7 networking specification](docs/stage-7-networking-spec.md). Stage 4's
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
