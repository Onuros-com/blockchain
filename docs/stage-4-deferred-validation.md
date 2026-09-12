# Stage 4 hardware validation status

Stage 4's GPU-mining prototype is hardware-qualified on both NVIDIA and AMD.
The node remains authoritative: it recomputes every proposed nonce and mix with
the pinned CPU KawPoW verifier before target comparison and persistence.

## NVIDIA evidence

- NVIDIA GeForce RTX 3060, 12 GiB, under Windows/WSL2.
- NVIDIA Docker runtime exposed CUDA 12.6.3.
- The KawPoW CUDA miner ran at approximately 18.39 MH/s.
- The local test endpoint accepted generated solutions.

## AMD evidence

- AMD Instinct MI300X under Ubuntu 24.04.3.
- ROCm 7.1.1, OpenCL 2.0 and architecture `gfx942:sramecc+:xnack-`.
- `amd-smi`, `rocminfo`, `hipcc` and a result-checked HIP kernel passed.
- The pinned OpenCL KawPoW worker built separately from the Onuros core.
- A 600-second block-30,000 run sustained 8.772604 MH/s mean and
  8.786889 MH/s maximum, with one accepted simulated solution.
- The hardware gate passed without a fatal error, device reset, invalid memory
  access or thermal shutdown.

The evidence summary and artifact hashes are recorded in
`docs/evidence/stage4-amd-mi300x-20260912.txt`.

## Consensus checks

- The published Ravencoin block-30,000 mix and final hashes match.
- Altered mix, nonce, preimage, target and result inputs are rejected.
- The local node-admission test persists only the CPU-verified block.
- The GPLv3 reference workers remain outside the Onuros core license boundary.

## Later integration and production hardening

The following are useful later checks, but are not Stage 4 prototype blockers:

- Test another NVIDIA and a consumer AMD RDNA architecture when practical.
- Run multi-hour thermal and power characterization on production miner builds.
- Mine against a real multi-node Onuros testnet and record network-level
  accepted/rejected shares after the Stage 7 external endpoint is deployed.

No paid cloud GPU should be left running after evidence is captured.
