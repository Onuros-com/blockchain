# Stage 7 evidence index

This index records implemented controls, automated results, physical evidence,
and open qualification work. It does not assign an audit disposition. Review
findings and acceptance decisions are added after the reviewer supplies the
audit points.

## Gate status

| Gate | Current evidence | Status |
|---|---|---|
| 1. Canonical framing and handshake on Linux and Windows | Core workflow run 34704080168 passed Ubuntu 24.04, Windows Server 2022, ASan/UBSan, and real-Orchard admission jobs | Automated gate passed |
| 2. Three fresh nodes synchronize from genesis | The loopback process gate and authenticated independent-host synchronization/evidence tooling check one durable tip | Tooling and loopback TLS gate passed; published-node run pending |
| 3. Restart and late synchronization | Three-process gate restarts a persisted client and checks height/tip recovery; state coordinator tests recover shielded state | Automated gate passed |
| 4. Competing branches converge by work | Chain-index tests and private commit coordinator tests cover a stronger-branch reorganization and restart after the block database switches | Automated gate passed |
| 5. Malformed, oversized and flooding traffic remains bounded | Frame decoder, event loop, admission, block transfer, fuzz, per-tick flood, sanitizer, and peer-policy tests in run 34704080168 | Automated gate passed |
| 6. 100 unique valid private transactions/s for 600 s across three published nodes | Real Orchard fixture verification sustained 120.218/s for 600.033 s; the real-FFI/TLS load executable and common-anchor corpus generator are implemented | Tooling implemented; physical 66,000-transaction run pending |
| 7. Near-16 MiB safety-bound propagation and validation | `stage7_block_limit` passed in run 34704080168; the real-FFI/TLS compact-block sender and durable receiver are implemented | Tooling implemented; physical run held for the agreed scalability and storage policy |
| 8. Crash-safe shutdown, restart and synchronization | Checksummed commit journal, durable ordering, corrupt-journal rejection, reorg recovery, event-loop clean shutdown | Automated gate passed |
| 9. Protocol, threat model, operator procedure and raw evidence | Networking specification, recovery design, threat model, independent TLS runbook, performance qualification, and GCP verifier log are committed | Documentation present; physical Gate 6/7 evidence pending |

## Automated workflow references

The agreed [Stage 7 scalability and storage hardening](stage-7-scalability-hardening.md)
extension preserves the original roadmap and sequences byte accounting,
compact batching, safe pruning, authenticated snapshots and sustainable block
production before physical Gate 7 acceptance. Proof aggregation remains a
separate research and review item; no second execution layer is introduced.

- Core workflow run
  [34704364613](https://github.com/Onuros-com/blockchain/actions/runs/34704364613)
  at commit `0f76a1625c40b73f046a37deccb77cc31d571c40`: all four jobs
  passed with the block fixture in the 99%--100% size window and the physical
  performance evidence validator enabled.
- Core workflow run
  [34704080168](https://github.com/Onuros-com/blockchain/actions/runs/34704080168)
  at commit `3b8b418fff4b79d90506b6e1bfd3cf8c8d9cf7d8`: all four jobs
  passed. Ubuntu included the first near-limit reconstruction gate and the
  independent-mode TLS loopback gate. Commit
  `94d032cbe58b5fd9a5053309b1bef049c7efe1bd` tightens the block fixture to the
  specified 99%--100% size window; its workflow result must be attached before
  audit acceptance. Windows compiled and executed the portable suite. Sanitizers
  and the pinned real-Orchard admission job passed.
- Core workflow run
  [34703534941](https://github.com/Onuros-com/blockchain/actions/runs/34703534941)
  at commit `f45043df379a30ee8acbf74a3fb35453ecd04879`: all jobs passed
  with stronger-branch shielded-state reorganization and restart recovery.
- Core workflow run
  [34703198500](https://github.com/Onuros-com/blockchain/actions/runs/34703198500)
  at commit `bd4e23ed2e2fc86ff47e1a2b04d11ae84fcb6842`: all jobs passed
  with the three-process private relay gate.

Workflow run numbers are GitHub-generated identifiers. The immutable source
commit and test output must be reviewed together.

## Physical and external evidence

### Independent mutual TLS

`evidence/stage7-tls-rtx4070-2026-09-12` records a source-restricted physical
TLS 1.3 exchange between the GCP origin and an RTX 4070 observer. Both peers
verified certificate identity and recorded `TLS_AES_256_GCM_SHA384`; the
evidence contains no private key. The RTX 4070 full 42-test qualification at
commit `6bbbd01b3a007ae02f34b83b3e6a1bfd8ba02e97` is stored in
`evidence/stage7-rtx4070-suite-2026-09-12`.

### Orchard verification duration

`docs/evidence/stage7-gcp-orchard-duration-20260912.txt` records:

- source commit `1091e91a4e5097ebafd8e80099eb090945c06699`;
- Google Compute Engine `c3d-standard-8`, AMD EPYC 9B14, four cores/eight
  threads, 30 GiB usable RAM;
- 72,135 real Orchard verifications over 600.033 seconds;
- 120.218 aggregate verifications/s;
- exit status zero and 109,236 KiB maximum resident set;
- benchmark log SHA-256
  `f7c13dd0ecddaeb6a54e6b91908b1be3a187bef493f318d964556bf4aff53cc3`.

This is verifier capacity using a repeated valid fixture. It is not unique
transaction relay or settlement evidence.

### Physical GPU mining

The separate `Onuros-com/Onuros-miner` repository contains:

- AMD MI300X/OpenCL qualification on
  `stage-7/shared-amd-nvidia`, commit `3e77eea`: one accepted share, zero
  rejected valid shares, one expected invalid-proof rejection, and recorded
  environment/build hashes;
- NVIDIA RTX 3060/CUDA qualification finalized at commit `09ee49a`: one
  accepted share, zero rejected valid shares, one expected invalid-proof
  rejection, and 20.592992 Mh/s simulation mean.

These runs qualify the external worker/node verification boundary. They do not
replace Stage 7 network Gates 6 or 7.

## Relevant source boundaries

- `core/include/onuros/tls_transport.hpp`: mutual TLS 1.3 and configured peer
  identity verification.
- `apps/stage7_tls_probe.cpp`: independent-mode checksummed ping/pong probe.
- `core/include/onuros/private_node_commit.hpp`: block/shielded-state journal,
  commit ordering, reorganization and recovery.
- `apps/stage7_private_relay_node.cpp`: three-process private relay harness.
- `apps/stage7_private_load_node.cpp`: physical real-Orchard, mutual-TLS relay
  load executable with origin, relay and observer roles.
- `apps/stage7_network_node.cpp`: loopback and fail-closed mutual-TLS
  independent-host synchronization with initial, late-join and restart modes.
- `scripts/capture-stage7-sync-evidence.sh` and
  `scripts/validate-stage7-sync-evidence.sh`: sanitized host capture and
  cross-host synchronization evidence validation.
- `docs/stage-7-published-sync-runbook.md`: physical published-host
  synchronization and restart procedure.
- `scripts/generate-stage7-private-corpus.sh`: non-overwriting generator for
  common-anchor, distinct valid Orchard transactions.
- `docs/stage-7-private-relay-runbook.md`: physical Gate 6 operating procedure.
- `apps/stage7_block_propagation_node.cpp`: real-Orchard compact-block sender
  and durable receiver for physical Gate 7.
- `docs/stage-7-block-propagation-runbook.md`: physical Gate 7 operating
  procedure and evidence boundary.
- `scripts/validate-stage7-evidence-bundle.sh`: cross-host commit, TLS CA,
  evidence-integrity and private-key exclusion gate layered over the numeric
  relay/block manifest validator.
- `core/include/onuros/peer_event_loop.hpp`: bounded multi-peer event loop and
  policy enforcement.
- `apps/stage7_bandwidth_benchmark.cpp`: compact-relay accounting and
  near-limit reconstruction/validation regression.
- `docs/stage-7-performance-qualification.md`: physical Gate 6 and Gate 7
  evidence contract.

## Evidence still required

1. Three-node, 600-second manifests for at least 60,000 distinct transactions
   that pass the real Orchard verifier.
2. Published-node near-limit block logs showing canonical size, mempool overlap,
   transfer bytes, independent validation, durable activation, restart state,
   and no more than 30.000 seconds per receiver.
3. Reviewer audit points and their resolution links.

## Evidence still required

1. Three-node, 600-second manifests for at least 60,000 distinct transactions
   that pass the real Orchard verifier.
2. Published-node near-limit block logs showing canonical size, mempool overlap,
   transfer bytes, independent validation, durable activation, restart state,
   and no more than 30.000 seconds per receiver.
3. Reviewer audit points and their resolution links.
