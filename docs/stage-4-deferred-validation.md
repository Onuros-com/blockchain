# Stage 4 deferred hardware validation

Stage 4's KawPoW GPU-mining prototype remains partially deferred while cloud
hardware access is pending. This does not invalidate the completed Stage 5 and
Stage 6 development milestones, but Stage 4 must not be described as fully
hardware-qualified.

## Evidence currently available

- NVIDIA GeForce RTX 3060, 12 GiB, under Windows/WSL2.
- NVIDIA Docker runtime successfully exposed CUDA 12.6.3.
- The KawPoW CUDA miner built and ran at approximately 18.39 MH/s.
- The local test endpoint accepted generated solutions.

This proves the NVIDIA prototype executes. It is not evidence of mining against
a public Onuros network because Stage 7 networking and testnet wiring do not yet
exist.

On 12 September 2026 an AMD Instinct MI300X RunPod reported Ubuntu 24.04.3,
ROCm 7.1.1 and architecture `gfx942`. `amd-smi`, `rocminfo` and `hipcc` all
detected the accelerator, and a compiled HIP kernel returned the expected value
42 with exit status 0. This clears the ROCm/HIP hardware smoke gate; it is not a
KawPoW hashrate result.

The active Stage 7 branch now contains a pinned CPU KawPoW verifier, a published
Ravencoin vector test, explicit invalid-proof results, and a local node-admission
test that rejects an altered GPU mix before persistence. The AMD benchmark
scripts keep the GPLv3 reference worker outside the Onuros core license.

## Still pending

- AMD KawPoW worker build and sustained mining run (hardware smoke passed).
- At least one additional NVIDIA architecture, if practical.
- Long-duration thermal/stability and invalid-share measurements.
- Mining against a real multi-node Onuros test network.
- CPU verification of received KawPoW solutions in the network block path.

CPU verification in the local block-admission path is implemented. Receipt over
the external multi-node network remains pending until the Stage 7 endpoint is
hardware-deployed.

The owner has requested AWS On-Demand G/VT vCPU quota and is waiting for AWS to
respond. AMD Developer Cloud access is also pending. No paid cloud GPU should be
left running after a test completes.

## Completion evidence to capture

For every device record the GPU model, driver, runtime/compiler, operating
system, miner commit, duration, average hash rate, accepted/rejected shares,
peak temperature, power draw when available, and exact command line. Logs must
not contain cloud credentials, wallet seeds or private keys.
