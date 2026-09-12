# Stage 4 audit record

The 12 September 2026 audit gives Stage 4 a final pass for its prototype scope.
The pinned CPU KawPoW consensus implementation, AMD MI300X OpenCL execution and
NVIDIA RTX 3060 CUDA execution are sound for their tested scope. Both physical
workers crossed the separate-process boundary and produced candidates accepted
only after independent Onuros CPU verification.

## Closed by this audit

- The AMD log gate now requires an identified `gfx` OpenCL device and a numeric
  hashrate greater than zero. Fatal runtime output, CPU-only output and zero
  hashrate fail regression tests, and an early worker exit fails the duration
  gate.
- The external worker build verifies its pinned commit, rejects unexpected
  tracked worker or submodule modifications and records the exact compatibility
  patch hash.
- ASan and UBSan now instrument the `onuros_kawpow` static library itself, not
  only its test executable.
- Core CI watches KawPoW scripts and the vendored consensus source tree.

## Closed physical integration gate

The dedicated loopback mining endpoint connects bounded wire candidates to
`onuros_kawpow` with fail-closed error handling. The qualification bridge sent
one deliberately invalid probe and one physical GPU share for each backend.
Both AMD/OpenCL and NVIDIA/CUDA runs recorded one accepted share, zero
unexpected rejected shares and one expected invalid rejection. Raw logs,
environment manifests, binary hashes and throughput evidence are preserved in
the separate `Onuros-miner` repository.

## Explicitly deferred to Stage 7

The general synchronization harness still uses its deterministic test
proof-of-work path. Stage 7 must integrate the bounded networking event loop
with the full node's real chain-state, mempool and shielded-state callbacks.
Public or independent-machine mining also requires authenticated TLS transport
and per-peer and per-address submission limits.

Passing this audit does not constitute a security audit, mainnet readiness or a
guarantee that production thermal and power limits are suitable.
