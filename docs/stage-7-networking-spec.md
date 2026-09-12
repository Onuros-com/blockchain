# Stage 7 specification: P2P networking and multi-node synchronization

Status: protocol engine, nonblocking TCP primitives, live loopback relay,
three-process synchronization, live headers-first validation, single-owner
download coordination, resumable chunk checkpoints, persistent peer discovery
and mutually authenticated TLS 1.3 transport implemented. The bounded multi-peer
event loop now accepts both raw private-test transports and authenticated TLS;
the first fail-closed KawPoW mining endpoint is implemented on loopback;
Windows and independent-machine testing remain open

## Purpose

Stage 7 turns the validated local private-chain components into multiple
independent nodes that discover peers, synchronize the strongest valid chain,
propagate private transactions and blocks, and remain consistent across restart
and reorganization. Wallet UI, explorer and public testnet deployment are later
milestones.

## Non-negotiable safety rules

- Peers are untrusted. No peer-provided boolean or metadata can bypass local
  proof, signature, economics, proof-of-work or state validation.
- Consensus data uses one canonical bounded encoding. Unknown versions and
  trailing bytes fail closed.
- Network parsing occurs before large allocation, decompression or expensive
  cryptographic verification.
- A node never adopts height alone; it selects the strongest fully validated
  chain by cumulative work.
- Private transaction contents must never be logged.
- Test keys and deterministic fixtures are forbidden from production binaries.

## Protocol layers

1. **Transport:** asynchronous TCP for the first private test network. The
   listening port is configurable; a permanent public default is chosen only
   after checking registry and deployment conflicts. Authenticated encrypted
   transport is required before a public testnet.
2. **Framing:** network magic, protocol version, message type, payload length,
   request identifier and checksum. Headers have a small fixed maximum.
3. **Handshake:** chain identifier, genesis hash, protocol range, services,
   node nonce, best height and cumulative work. A node rejects self-connections,
   wrong chains, incompatible versions and duplicate peer sessions.
4. **Synchronization:** headers first, then bounded block chunks. Every header,
   proof-of-work transition and block is validated locally before activation.
5. **Relay:** inventory announcements for transactions and blocks, bounded
   get-data requests, duplicate suppression and deterministic local admission.

## Required messages

- `hello`, `hello_ack`, `disconnect`
- `ping`, `pong`
- `get_headers`, `headers`
- `get_block`, `block_chunk`
- `tx_inventory`, `get_transactions`, `transactions`
- `block_inventory`
- dedicated mining service: `mining_job`, `mining_solution`, `mining_result`

Every message type receives explicit byte, item-count, nesting and time limits.
Unknown message types are ignored or rejected according to negotiated protocol
version; they never reach consensus code accidentally.

## Large private blocks

A two-action Stage 6 private transaction is roughly 9 KB. At 100 transactions
per second and a 60-second block target, a full interval can approach 6,000
transactions and approximately 55 MB before framing overhead. Stage 7 must not
assume that a complete block fits safely in one small network message.

- The initial private-testnet consensus ceiling is exactly 16 MiB
  (16,777,216 serialized bytes), enforced from one shared constant by block
  decoding, contextual validation and persistent storage. P2P framing must use
  the same constant when implemented.
- Blocks are transferred in independently bounded chunks.
- Chunk order, total size, block identifier and final checksum are committed
  before activation.
- Validation streams from bounded storage rather than duplicating an entire
  block in each peer buffer.
- In-flight bytes, chunks and block requests are limited globally and per peer.
- The 16 MiB ceiling is a safety bound, not a claim that the current roughly
  9 KB transaction format can settle 100 TPS on-chain. Current encoding fits
  roughly 1,800 two-action transactions per full block (about 30 TPS at a
  60-second interval). Reaching the roadmap's 100 TPS settlement target without
  restoring 55 MB blocks requires measured transaction/proof-size reduction or
  safe aggregation; relay and verification benchmarks are reported separately.

## Compact block relay and lightweight nodes

The hard block ceiling is not the normal network payload target. Transactions
are first propagated into peer mempools. When a miner announces a block, it
sends the 160-byte header plus the ordered 256-bit transaction identifiers.
A peer reconstructs the block from transactions it already has and requests
only the missing indexes. The reconstructed full block must pass the ordinary
transaction-root, proof, state and consensus checks before activation.

The initial format deliberately uses full transaction identifiers. For roughly
1,800 transactions, its block announcement is about 58 KiB instead of as much
as 16 MiB when mempools overlap. A later measured protocol version may use
keyed short identifiers, but only with explicit collision recovery and no
consensus dependency on those identifiers.

A lightweight Onuros node may use the same relay plus pruned historical block
bodies. It still verifies headers, cumulative work, shielded-state transitions
and every newly accepted block. It is not allowed to trust a relay appliance or
replace local consensus validation. Initial synchronization still requires a
trusted checkpoint or enough historical data to validate from genesis; that
trade-off must be explicit rather than hidden behind the word "lightweight".

### Implemented protocol-engine checkpoint

- Canonical checksummed P2P frames with network magic, protocol negotiation,
  message type, request identifier and pre-allocation payload bounds.
- Cross-platform IPv4 TCP listener/connection wrappers with nonblocking I/O and
  a bounded incremental stream decoder. A real loopback test exchanges a
  fragmented handshake, transaction inventory, compact block, missing-index
  request and transaction chunk in both directions.
- Handshake rejection for wrong chain/genesis, self-connections, incompatible
  versions, missing services and unauthenticated transport when public-mode
  policy requires it. Transport encryption itself is not yet implemented.
- Bounded transaction inventory with duplicate suppression.
- Global/per-address connection ceilings, handshake/idle deadlines, bounded
  queues, request and validation-job backpressure, misbehavior scoring and
  temporary bans.
- Compact block reconstruction from validated relay-pool transactions.
- Ordered missing-transaction requests and bounded multi-chunk responses with
  consistent manifests, byte/count ceilings and transaction-ID verification.
- A deterministic three-node harness with isolated databases, different
  mempool overlap, independent block validation, convergence and restart.
- A real three-process loopback harness: one node mines and serves a compact
  multi-height chain, two separately persisted nodes exchange locators, validate
  its header chain, request only then the missing transactions, reconstruct and
  independently validate each block, then a restarted node proves durable tip
  recovery. Each repeat uses isolated evidence files.
- A mini-node retention engine that preserves all headers and local header-chain
  commitments while retaining a bounded recent body window. Checkpoints are
  local commitments, not trusted or signed network checkpoints.
- Canonical bounded header locator and header-batch encodings. A batch is
  validated atomically for linkage, height, median time, future time, target,
  proof of work and accumulated-work overflow before it can change the best
  header tip. The default 1,600-header batch remains below a 256 KiB frame.
- One active peer lease per block download with expiry/failover, plus cross-peer
  transaction-request deduplication. Duplicate or late peers cannot silently
  create parallel 16 MiB downloads.
- Out-of-order resumable transaction chunks with bounded checkpoint encoding,
  exact duplicate idempotence, conflicting-duplicate rejection and missing
  sequence discovery. Corrupt checkpoints restore atomically or not at all.
- Bandwidth accounting separates useful, duplicate, avoided-duplicate and
  resumed bytes without recording private transaction contents.
- Persistent peer discovery uses a bounded, checksummed database with atomic
  replacement, deterministic candidate selection and capped exponential retry
  backoff. Invalid addresses, duplicate records and corrupt databases fail
  closed.
- The authenticated transport uses TLS 1.3 with certificates required from
  both peers and validated against an operator-configured trust root. TLS
  compression and renegotiation are disabled. Tests generate temporary
  one-day credentials; no private key is committed to the repository.
- The multi-peer event loop applies per-peer read, write and frame budgets,
  bounded send queues, handshake and idle deadlines, partial-write recovery,
  malformed-peer isolation, traffic counters and clean shutdown. Its transport
  interface admits TLS only after the cryptographic handshake has completed;
  the protocol handshake can require that authenticated state.
- Core CI now runs for every Stage 7 branch on both Ubuntu and Windows Server.
  Platform-neutral unit and live socket tests run on Windows; Bash orchestration
  tests remain Linux-only. A green Windows job is required before the Windows
  execution gate is marked complete.
- The dedicated mining service retains each complete candidate inside the node
  and sends a fixed-size, versioned KawPoW job containing only job ID, height,
  header hash and compact target. A miner returns only job ID, nonce and mix.
  The node rejects unknown, stale and replayed jobs and recomputes KawPoW on the
  CPU before durable admission. A real two-process loopback test accepts a valid
  candidate and rejects an altered mix without persistence.

### Mining endpoint boundary

The initial executable listens on loopback only and processes one bounded
solution per connection. This is intentional: it proves the consensus boundary
without exposing an unauthenticated expensive-verification service. Before
public or independent-machine use, the same message payloads must run over the
authenticated TLS transport with per-peer and per-address submission limits.

AMD and NVIDIA workers will live together in the separate `Onuros-miner`
repository. The node and CPU verifier remain in this repository. Packaging may
launch both executables from one UI, but GPU code is never linked into or trusted
by consensus.

The 1,800-transaction benchmark (9,173 encoded bytes per transaction) measured
the following block-relay phase. These figures exclude the earlier transaction
gossip that populated each peer's mempool:

| Mempool overlap | Relay bytes | Saving vs full block |
| ---: | ---: | ---: |
| 25% | 12,454,698 | 24.57% |
| 50% | 8,322,416 | 49.60% |
| 90% | 1,710,744 | 89.64% |
| 100% | 57,764 | 99.65% |

This checkpoint does not complete Stage 7. The loop must still be integrated
with the full node's chain-state callbacks; Windows execution,
independent-machine testing and the sustained ten-minute private-transaction
gate are also required. The
cross-platform code is structured for Winsock and links `ws2_32`. Windows Server
2022 compilation and execution passed in the Stage 7 CI matrix at commit
`13b30a58c0e63683d3952cedf088dbc57a0ae9a0`; physical Windows hardware remains
useful for later GPU and packaging validation, not this core networking gate.

### Sustained private-load evidence rules

The duration gate runs the real pinned Orchard verifier with a shared start
barrier and fails below the configured TPS floor. Its default is four workers,
600 seconds and 100 verifications per second. Reusing one valid fixture measures
real proof-verification capacity, but it does not represent distinct nullifiers
and therefore is not called unique mempool admission or settlement. Stage 7
still requires a separate three-node relay result with unique admitted
transactions; these measurements must not be combined into a misleading TPS
number.

The sustained verification portion passed on 12 September 2026 at source
`1091e91a4e5097ebafd8e80099eb090945c06699`. A Google Compute Engine
`c3d-standard-8` in `us-central1` ran eight verifier workers on one AMD EPYC
9B14 socket (four physical cores, eight threads), Ubuntu 26.04.1, 30 GiB usable
RAM and no swap. It completed 72,135 real Orchard verifications over 600.033
measured seconds at 120.218 aggregate verifications per second. The process
exited zero, used 787% CPU and reached 109,236 KiB maximum resident memory. The
original benchmark log has SHA-256
`f7c13dd0ecddaeb6a54e6b91908b1be3a187bef493f318d964556bf4aff53cc3`;
the captured result and its limitations are preserved in
`docs/evidence/stage7-gcp-orchard-duration-20260912.txt`.

## Peer and denial-of-service controls

- Maximum inbound/outbound peers and per-IP connection limits.
- Handshake, idle and request timeouts.
- Bounded send/receive queues with backpressure.
- Rate limits for inventory, transactions, headers and expensive validation.
- Misbehavior scoring for malformed frames, invalid proofs/blocks, unsolicited
  bulk data and repeated timeouts.
- Temporary bans with bounded persisted state; no consensus decision depends on
  a peer identity or ban score.
- Orphan and duplicate caches have strict count, byte and lifetime limits.

## Concurrency model

- Network I/O does no consensus mutation directly.
- Parsed candidates enter bounded validation queues.
- Orchard verification uses a reusable worker pool and the cached immutable
  verifying key.
- One ordered chain-state executor commits accepted blocks and reorganizations.
- Cancellation and shutdown drain or discard work without partially committing
  shielded state.

## Synchronization and reorganization

- Download competing headers and calculate cumulative work exactly.
- Request blocks from more than one peer when practical, without accepting the
  same work twice.
- Stage a candidate branch, validate all transitions, then atomically replace
  active block and shielded state.
- After restart, reconcile the block database and shielded-state tip before
  opening network admission.
- Revalidate the private mempool after every activated tip change.

## Privacy requirements

- Handshakes and logs contain no wallet address, viewing key or transaction
  ownership metadata.
- Transaction relay timing can reveal origin; initial tests record this risk.
  Randomized diffusion or stem/fluff relay is evaluated before public testnet.
- No master viewing key is introduced by networking.
- Metrics expose counts, sizes and latency, never private transaction contents.

## Observability

Expose local structured metrics for peer count, queue depth, header/block sync,
bytes, validation latency, Orchard verification rate, block propagation,
reorganizations and rejection categories. Metrics and logs must be safe to
publish after removing IP addresses where appropriate.

## Stage 7 test topology

- First gate: three nodes as separate local processes with isolated databases
  and ports.
- Second gate: three independent machines or cloud instances in at least two
  network locations.
- Inject valid transactions through one node and require convergence at all
  nodes.
- Include slow, disconnecting, malformed and adversarial peers.

## Completion gates

1. Canonical framing and handshake tests pass on Linux and Windows.
2. Three fresh nodes discover/connect and synchronize from genesis.
3. Restarted and late-joining nodes reach the same tip and shielded root.
4. Competing valid branches resolve identically by cumulative work.
5. Malformed frames, oversized declarations, invalid proofs/blocks and flooding
   cannot bypass limits or corrupt state under ASan/UBSan.
6. At least 100 sustained valid private transactions per second are admitted
   and relayed for ten minutes across three published nodes without divergence.
7. Blocks at the selected testnet limit propagate and validate within the
   documented latency budget; confirmation latency is reported separately.
8. Clean shutdown/restart during synchronization preserves a recoverable state.
9. Protocol, threat model, operator guide and raw benchmark evidence are
   published.

If Gate 6 or 7 fails, Stage 7 remains incomplete and measurements determine
whether to optimize verification, block limits, relay or transaction design.
