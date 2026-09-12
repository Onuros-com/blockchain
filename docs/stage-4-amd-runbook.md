# Stage 4 AMD KawPoW qualification runbook

This runbook qualifies an AMD GPU worker without trusting it for consensus.
The worker proposes a nonce and mix hash. `onuros_kawpow` independently
recomputes KawPoW on the CPU before target comparison and block persistence.

## Security and licensing boundary

- The node verifier is pinned to Ravencoin commit
  `6d48ae0175b10283248146ae3080e2ba70966739`; its MIT and Apache-2.0 notices
  are preserved under `third_party/ravencoin-kawpow`.
- The hardware worker is built separately from
  `RavenCommunity/kawpowminer` commit
  `632f6ea0a5cd09e2c6443374dbe6db0a767715ba`, which is GPLv3.
- The GPL worker is not copied into, statically linked with, or relicensed as
  the Onuros core.
- A GPU result is never authoritative. The CPU verifier rejects a changed mix,
  nonce, preimage, target or result.

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
hashes. Stop rather than terminate the pod after evidence is captured.

## Pass conditions

1. `make kawpow-test` matches the published block-30,000 mix and final hashes.
2. The node-path test rejects an altered mix and persists only the verified
   block.
3. OpenCL identifies the intended AMD device, not a CPU implementation.
4. The miner reports real non-zero KawPoW hashrate for the requested duration.
5. No fatal error, device reset, invalid memory access or thermal shutdown is
   present in the log.

The reference benchmark qualifies GPU execution. Accepted/rejected network
share counts require the Stage 7 external multi-node endpoint and remain a
separate final integration gate.
