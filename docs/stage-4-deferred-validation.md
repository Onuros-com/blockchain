# Stage 4 hardware validation status

Stage 4's GPU workers have executed on both NVIDIA and AMD hardware. The pinned
Onuros CPU KawPoW verifier separately passes consensus vectors and rejection
tests. The two paths are not yet connected end to end: a GPU-produced nonce and
mix has not been submitted into the runnable node's CPU verifier.

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

## Required Stage 7 integration gate

Before calling the mining path production-integrated:

- retain the completed fail-closed loopback `onuros_kawpow` endpoint while the
  general synchronization harness continues using deterministic test work;
- add authenticated public binding and submission rate limits;
- submit a GPU-produced nonce and mix from `Onuros-miner`;
- record CPU-verifier acceptance of the valid candidate and rejection of a
  deliberately altered candidate; and
- repeat a short AMD run against that endpoint.

The bounded wire protocol, stale/replay rejection and valid/altered two-process
loopback tests are implemented. The remaining tasks extend that Stage 7 work;
they do not change the roadmap or invalidate the completed MI300X execution
evidence.

## Later hardware hardening

The following are useful later checks, but are not Stage 4 prototype blockers:

- Test another NVIDIA and a consumer AMD RDNA architecture when practical.
- Run multi-hour thermal and power characterization on production miner builds.
- Run multi-node endurance and fault-injection tests after the Stage 7
  integration gate is deployed.

No paid cloud GPU should be left running after evidence is captured.
