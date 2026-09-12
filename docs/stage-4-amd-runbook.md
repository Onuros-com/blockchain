# Stage 4 AMD KawPoW qualification runbook

This runbook qualifies execution of an AMD GPU worker without trusting it for
consensus. The current reference benchmark proves that the pinned worker hashes
on the selected AMD device. Directly submitting its nonce and mix hash to
`onuros_kawpow` is a separate Stage 7 integration gate.

## Security and licensing boundary

- The node verifier is pinned to Ravencoin commit
  `6d48ae0175b10283248146ae3080e2ba70966739`; its MIT and Apache-2.0 notices
  are preserved under `third_party/ravencoin-kawpow`.
- The hardware worker is built separately from
  `RavenCommunity/kawpowminer` commit
  `632f6ea0a5cd09e2c6443374dbe6db0a767715ba`, which is GPLv3.
- The GPL worker is not copied into, statically linked with, or relicensed as
  the Onuros core.
- A future GPU submission must never be authoritative. The existing CPU
  verifier rejects a changed mix, nonce, preimage, target or result.

## RunPod MI300X procedure

Use the existing ROCm 7.1.1 Ubuntu 24.04 pod and persistent `/workspace`
volume. Start the paid GPU only when ready to execute these steps.

```bash
apt-get update
apt-get install -y git cmake make g++ gcc ocl-icd-opencl-dev opencl-headers \
  libboost-all-dev

cd /workspace
git clone --branch stage-4/amd-kawpow-completion \
  https://github.com/Onuros-com/blockchain.git onuros-node
cd /workspace/onuros-node
make kawpow-test
./scripts/build-amd-kawpow-reference.sh
```

The last command prints the exact miner path. Pass that path to the ten-minute
gate:

```bash
./scripts/run-amd-kawpow-gate.sh \
  /workspace/kawpowminer-reference/build-onuros-amd/kawpowminer/kawpowminer \
  600 30000 /workspace/onuros-stage4-evidence
```

If the build places the executable elsewhere, use the `miner=` path printed by
the build script. The gate records OS, driver/runtime, device, binary hash,
command, output, exit status, temperature/power metrics when available, and log
hashes. Its validator requires a `gfx` OpenCL device and a numeric hashrate
greater than zero; CPU-only and `0.00 Mh` logs fail. Stop rather than terminate
the pod after evidence is captured.

The gate also requires GNU `timeout` status 124, proving that the requested
duration elapsed. A worker that exits early does not qualify even if its partial
log contains a non-zero hashrate.

The build helper verifies the pinned checkout and rejects unexpected tracked
worker or submodule changes. It then applies two narrow source-compatibility
shims required by the pinned 2019 worker on Ubuntu 24.04: a fixed Linux
`PTHREAD_STACK_MIN` for its old bundled Boost and an explicit `<cstdint>`
include. These affect only the separate GPL reference worker build; they do not
change Onuros consensus code.

## Recorded MI300X qualification

The 12 September 2026 RunPod qualification used one AMD Instinct MI300X
(`gfx942:sramecc+:xnack-`), ROCm 7.1.1 and OpenCL 2.0. The 600-second gate at
block 30,000 reported a mean of 8.772604 MH/s, a maximum of 8.786889 MH/s and
one accepted simulated solution. It completed without a fatal error, device
reset, invalid memory access or thermal shutdown.

- Gate: `amd_kawpow_gate=PASS duration_seconds=600 block=30000`
- Miner SHA-256: `fc7a1f9223aadb6990d91e7a9bf158f9691c470b237df9eb11f6b6bfa2023fd1`
- 600-second log SHA-256: `bca9596504396b1df17a154167f8fefe1e74e5d688cbde604464f91ce2b50a02`

## Pass conditions

1. `make kawpow-test` matches the published block-30,000 mix and final hashes.
2. The node-path test rejects an altered mix and persists only the verified
   block.
3. OpenCL identifies the intended AMD device, not a CPU implementation.
4. The miner reports real non-zero KawPoW hashrate for the requested duration.
5. No fatal error, device reset, invalid memory access or thermal shutdown is
   present in the log.

The reference benchmark qualifies GPU execution only. A GPU-produced nonce and
mix must still be accepted by the Onuros CPU verifier and an altered candidate
must be rejected through the Stage 7 external multi-node endpoint. That remains
the final integration gate.
