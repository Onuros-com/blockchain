# Stage 7 evidence index

This index records implemented controls, automated results, physical evidence,
and open qualification work. It does not assign an audit disposition. Review
findings and acceptance decisions are added after the reviewer supplies the
audit points.

## Gate status

| Gate | Current evidence | Status |
|---|---|---|
| 1. Canonical framing and handshake on Linux and Windows | Core workflow run 34704080168 passed Ubuntu 24.04, Windows Server 2022, ASan/UBSan, and real-Orchard admission jobs | Automated gate passed |
| 2. Three fresh nodes synchronize from genesis | `run-stage7-three-process.sh` starts three processes with isolated databases and checks one durable tip | Loopback process gate passed; published-node run pending |
| 3. Restart and late synchronization | Three-process gate restarts a persisted client and checks height/tip recovery; state coordinator tests recover shielded state | Automated gate passed |
| 4. Competing branches converge by work | Chain-index tests and private commit coordinator tests cover a stronger-branch reorganization and restart after the block database switches | Automated gate passed |
| 5. Malformed, oversized and flooding traffic remains bounded | Frame decoder, event loop, admission, block transfer, fuzz, per-tick flood, sanitizer, and peer-policy tests in run 34704080168 | Automated gate passed |
| 6. 100 unique valid private transactions/s for 600 s across three published nodes | Real Orchard fixture verification sustained 120.218/s for 600.033 s; deterministic three-process relay passed for one transaction | Pending: existing results do not prove 60,000 distinct valid transactions or published-node convergence |
| 7. Near-16 MiB block propagation and validation | `stage7_block_limit` passed in run 34704080168; compact-relay bandwidth benchmark recorded | In-process safety gate passed; independent-node latency and durable shielded activation pending |
| 8. Crash-safe shutdown, restart and synchronization | Checksummed commit journal, durable ordering, corrupt-journal rejection, reorg recovery, event-loop clean shutdown | Automated gate passed |
| 9. Protocol, threat model, operator procedure and raw evidence | Networking specification, recovery design, threat model, independent TLS runbook, performance qualification, and GCP verifier log are committed | Documentation present; physical Gate 6/7 evidence pending |

## Automated workflow references

- Core workflow run
  [34704080168](https://github.com/Onuros-com/blockchain/actions/runs/34704080168)
  at commit `3b8b418fff4b79d90506b6e1bfd3cf8c8d9cf7d8`: all four jobs
  passed. Ubuntu included the near-limit block gate and independent-mode TLS
  loopback gate. Windows compiled and executed the portable suite. Sanitizers
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
- `core/include/onuros/peer_event_loop.hpp`: bounded multi-peer event loop and
  policy enforcement.
- `apps/stage7_bandwidth_benchmark.cpp`: compact-relay accounting and
  near-limit reconstruction/validation regression.
- `docs/stage-7-performance-qualification.md`: physical Gate 6 and Gate 7
  evidence contract.

## Evidence still required

1. Server and client TLS manifests from two independent machines using the
   documented peer-name and source-firewall restrictions.
2. Three-node, 600-second manifests for at least 60,000 distinct transactions
   that pass the real Orchard verifier.
3. Published-node near-limit block logs showing canonical size, mempool overlap,
   transfer bytes, independent validation, durable activation, restart state,
   and no more than 30.000 seconds per receiver.
4. Reviewer audit points and their resolution links.
