# Stage 6 completion report

Status: complete development milestone

Stage 6 integrates mandatory private transaction admission into the Onuros
local blockchain core. It is pre-mainnet software and has not received an
independent security audit.

## Delivered

- Canonical, bounded `ONP2` private transaction encoding.
- A fail-closed C++ verification boundary backed by the pinned official
  Orchard implementation at commit
  `f2be3a479837df6583110fd44e124d155ec592ee`.
- Halo2 proof, RedPallas spend authorization and binding-signature checks.
- Exact binding between verified anchor, fee, nullifiers and commitments and
  the decoded transaction.
- Duplicate-spend and commitment rejection in block admission and the bounded
  private mempool.
- Deterministic fee-first mempool selection and tip-change revalidation.
- Shielded-root commitment in the block header and checksummed persistent
  shielded state with atomic connect, disconnect and reorganization.
- Canonical private reward validation against the 50 ONUROS schedule and
  transaction fees.
- A real end-to-end test that signs a spendable Orchard transaction over the
  canonical Onuros digest and processes it through the C++ node path.

## Performance evidence

Hardware and environment:

- AMD Ryzen 5 3600, 6 cores / 12 logical CPUs;
- 7.7 GiB RAM and 2.0 GiB swap allocated to WSL2;
- Linux 6.6.114.1-microsoft-standard-WSL2;
- release build, four independent verifier processes;
- GPU not used.

Three consecutive runs each verified 4,000 real pinned-Orchard proofs:

| Run | Verified | Seconds | Aggregate TPS |
| --- | ---: | ---: | ---: |
| 1 | 4,000 | 26.1025 | 153.242 |
| 2 | 4,000 | 26.1292 | 153.085 |
| 3 | 4,000 | 26.1278 | 153.094 |

Mean throughput was 153.140 TPS across 12,000 successful verifications. The
range was 0.157 TPS, approximately 0.1% of the mean. This exceeds the initial
100 sustained private-verification TPS checkpoint.

The result measures repeated verification of a valid real Orchard fixture. It
does not measure unique wallet proof creation, block propagation, disk I/O or
multi-node confirmation latency. Those are Stage 7 measurements.

## Security validation

Automated tests cover malformed and truncated encodings, unsupported versions,
size/action limits, invalid value balance, proof and signature failure,
authenticated-effect mismatch, unknown anchors, duplicate transactions,
repeated nullifiers and commitments, fee overflow, invalid rewards, invalid
transaction and shielded roots, persistence corruption, stale preparation,
restart, disconnect and atomic reorganization failure.

The release checkpoint also runs deterministic mutation and random-input
decoder tests under ASan and UBSan. These tests improve robustness evidence but
are not a replacement for coverage-guided fuzzing or an external audit.

## Evidence

- Verifying-key cache: `18953ba`
- Real Orchard node-pipeline merge: `9b26912`
- Stage 6 final status: `5092100`
- Core CI run: `34654366681`
- Orchard integration run: `34654366599`

## Known limitations

- No P2P networking or peer synchronization yet.
- No network-level TPS or confirmation-latency result yet.
- No production wallet proving pipeline yet.
- Stage 5 and Stage 6 database formats are intentionally incompatible.
- Stage 4 AMD mining evidence remains deferred.
- Passing tests does not mean the implementation is audited or safe for funds.

