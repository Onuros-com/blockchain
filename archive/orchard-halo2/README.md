# Orchard/Halo2 historical implementation archive

Status: superseded historical reference; not built, run, or selected by any
active CMake target, Make target, GitHub workflow, configuration default, or
qualification script.

The source and tests remain in the repository so earlier Stage 6 and Stage 7
measurements can be reproduced from their pinned commits. The former automated
workflow was removed from `.github/workflows` when Groth16/BLS12-381 + Poseidon
became the sole active testnet candidate on 2026-09-15.

Historical evidence, including commit `6662c32`, Gate 6.5 records, the
600.033-second 120.218-verifications/s result, and corrections-log entries was
not deleted. Such records must carry:

```text
evidence_status=superseded-historical
active_privacy_protocol_qualified=false
```

Nothing in this directory is evidence for the active protocol or permission to
reactivate Orchard/Halo2. Any comparative reproduction must occur from a
detached historical commit and must publish a new, clearly non-current report.
