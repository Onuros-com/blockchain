# Onuros Local Blockchain Core

This repository contains the C++17 implementation of the Onuros local blockchain core.

Development branch: `local-blockchain`.

Confirmed economics:

- 50 ONUROS initial block subsidy
- 60-second target block interval
- Halving every 2,102,400 blocks
- Permanent 0.5 ONUROS subsidy floor
- 10% project team/ecosystem allocation
- 60-block reward maturity
- No premine

Stage 5 prototype checkpoints are complete. The branch includes canonical block and
transaction encoding, bounded decoding, contextual validation, exact compact-target
and chain-work arithmetic, 60-block difficulty adjustment, median-time rules, a
checksummed append-only database with torn-write recovery, strongest-chain selection,
atomic reorganization state, and an integrated local validation/mining/restart pipeline.

Run `make test` or use CMake/CTest. Build the runnable local node with `make node`,
then follow [the WSL local-node guide](docs/local-node-run.md). Linux, Windows and
sanitizer builds run in GitHub Actions.

The local miner uses an injected proof-of-work hash so consensus behavior can be tested
deterministically. Stage 4's KawPoW implementation still needs its final AMD hardware
evidence and later testnet wiring. Stage 6 will define the mandatory private-transaction
body and shielded state. The first performance goal remains 100 sustained private TPS
across multiple nodes on published hardware; it is a target, not a current claim.
