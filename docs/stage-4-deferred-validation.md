# Stage 4 hardware validation status

Stage 4's physical GPU qualification is complete for its prototype scope. Real
AMD/OpenCL and NVIDIA/CUDA workers submitted KawPoW nonce and mix candidates
through the separate-process qualification bridge, and the Onuros node
independently recomputed each candidate with its pinned CPU verifier.

## Completed NVIDIA evidence

- NVIDIA GeForce RTX 3060, 12 GiB, compute capability 8.6.
- Ubuntu 26.04.1 WSL2 host and Ubuntu 22.04.5 CUDA qualification container.
- NVIDIA driver 610.62 and CUDA toolkit 12.6.
- The pinned CUDA worker was compiled for `sm_86`.
- The node rejected the deliberately invalid first submission and accepted the
  physical GPU candidate as submission 2.
- The bridge recorded one accepted share, zero unexpected rejected shares and
  one expected invalid rejection.
- A 60-second block-1 benchmark measured 20.592992 MH/s mean and 20.600608
  MH/s maximum.

## Completed AMD evidence

- AMD Instinct MI300X under Ubuntu 24.04.3.
- ROCm 7.1.1, OpenCL 2.0 and architecture `gfx942:sramecc+:xnack-`.
- `amd-smi`, `rocminfo`, `hipcc` and a result-checked HIP kernel passed.
- The node rejected the deliberately invalid first submission and accepted the
  physical GPU candidate as submission 2.
- The bridge recorded one accepted share, zero unexpected rejected shares and
  one expected invalid rejection.
- A 60-second block-1 benchmark measured 8.588 MH/s mean, 7.880 MH/s minimum
  and 8.770 MH/s maximum.
- The pinned external OpenCL worker required a bounded result-count
  compatibility patch for its fixed result buffer.

## Consensus boundary

- The published Ravencoin block-30,000 mix and final hashes match.
- Altered mix, nonce, preimage, target and result inputs are rejected.
- A GPU worker never decides consensus: the node retains the block template and
  independently recomputes every submitted nonce and mix on the CPU.
- The local node persists only CPU-verified candidates.
- The GPLv3 reference workers remain outside the Onuros core license boundary.
- The complete raw physical evidence and hashes are preserved in
  [Onuros-miner](https://github.com/Onuros-com/Onuros-miner/tree/stage-7/shared-amd-nvidia/evidence).

## Remaining Stage 7 work

Stage 4's loopback integration gate is closed. Stage 7 must still integrate the
network event loop with the full node's real chain, mempool and shielded state.
Before independent-machine or public mining, the mining payloads must run over
authenticated TLS with per-peer and per-address submission limits.

## Later hardware hardening

The following are useful later checks, but are not Stage 4 prototype blockers:

- Test another NVIDIA and a consumer AMD RDNA architecture when practical.
- Run multi-hour thermal and power characterization on production miner builds.
- Run multi-node endurance and fault-injection tests after the Stage 7
  integration gate is deployed.

No paid cloud GPU should be left running after evidence is captured.
