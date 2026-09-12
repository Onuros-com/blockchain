# Stage 4 audit record

The 12 September 2026 audit gives Stage 4 a conditional pass: the pinned CPU
KawPoW consensus implementation and AMD MI300X execution evidence are sound for
their tested scope. End-to-end GPU-to-node consensus integration remains a
Stage 7 gate.

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

## Explicitly deferred to Stage 7

The runnable node still uses its deterministic test proof-of-work path. Stage 7
must connect incoming GPU candidates to `onuros_kawpow` with fail-closed error
handling, then demonstrate valid acceptance and altered-candidate rejection on
an external node. A short AMD rerun will close that final integration gate.

Passing this audit does not constitute a security audit, mainnet readiness or a
guarantee that production thermal and power limits are suitable.
