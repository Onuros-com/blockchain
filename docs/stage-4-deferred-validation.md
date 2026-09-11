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

## Still pending

- AMD GPU build and sustained mining run.
- At least one additional NVIDIA architecture, if practical.
- Long-duration thermal/stability and invalid-share measurements.
- Mining against a real multi-node Onuros test network.
- CPU verification of received KawPoW solutions in the network block path.

The owner has requested AWS On-Demand G/VT vCPU quota and is waiting for AWS to
respond. AMD Developer Cloud access is also pending. No paid cloud GPU should be
left running after a test completes.

## Completion evidence to capture

For every device record the GPU model, driver, runtime/compiler, operating
system, miner commit, duration, average hash rate, accepted/rejected shares,
peak temperature, power draw when available, and exact command line. Logs must
not contain cloud credentials, wallet seeds or private keys.

