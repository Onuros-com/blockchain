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

Stage 5 currently includes canonical block and transaction encoding, transaction Merkle roots, strict bounded decoding, contextual block validation, overflow-checked 256-bit accumulated work, strongest-chain selection, and reorganization planning.

Run `make test` or use CMake/CTest. The first performance goal remains 100 sustained private TPS across multiple nodes on published hardware; this is a target, not a current performance claim.
