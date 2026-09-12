# Pinned KawPoW consensus dependency

This directory contains the CPU consensus implementation from Ravencoin commit
`6d48ae0175b10283248146ae3080e2ba70966739`. The Ethash/ProgPoW files identify
their Apache-2.0 licensing in their source headers; Ravencoin's repository-level
MIT notice is preserved in `COPYING`.

Onuros uses this code only as the independently executed node verifier. GPU
miners are separate, untrusted producers of nonce and mix-hash candidates.
Every received candidate is recomputed by this CPU implementation before target
comparison or durable block admission.
