# Stage 7 network threat model

## Scope

This document covers the Stage 7 peer transport, framing, transaction and block
relay, synchronization, persistent peer state, mining endpoint, and coordinated
block/shielded-state commit path. Wallet key custody, GUI packaging, public seed
operation, and production certificate authority procedures are outside this
stage.

## Assets and invariants

The implementation must preserve:

- the active chain selected by validated cumulative work;
- the shielded-state root and nullifier set corresponding to that active tip;
- local mempool admission rules and transaction uniqueness;
- bounded memory, disk, CPU, peer slots, and validation workers;
- private transaction payload confidentiality in logs and metrics;
- operator trust roots and endpoint private keys.

No remote peer, relay cache, miner, benchmark fixture, or persisted peer score is
a consensus authority.

## Trust boundaries

Network bytes are hostile until canonical frame decoding and message-specific
bounds succeed. TLS authenticates an operator-issued peer identity; it does not
make that peer's blocks or transactions valid. GPU miners return candidate
nonces and mixes only. The CPU mining endpoint recomputes KawPoW before
submission. Orchard authorization is accepted only through the pinned verifier
backend in the production admission composition.

Block storage and shielded state are separate durable resources. Their
write-ahead journal is part of the recovery boundary and is checksummed, but it
is not assumed immune to arbitrary disk replacement by a privileged attacker.

## Threats and controls

| Threat | Control | Residual condition |
|---|---|---|
| Malformed or oversized frames | Fixed header, network magic, protocol range, known message set, pre-allocation length limit, checksum, bounded incremental decoder | Authenticated peers can still consume their configured byte and frame budgets |
| Handshake confusion or cross-network connection | Chain ID, genesis hash, nonce, service flags, version negotiation, optional authenticated-transport requirement | Operators must distribute the correct trust root and names |
| Peer impersonation or interception | TLS 1.3, required client certificate, CA validation, configured DNS-name verification on both sides, no renegotiation or compression | Compromised CA or endpoint key permits impersonation until revocation or rotation |
| Sybil or eclipse pressure | Global and per-address peer ceilings, deterministic candidate selection, retry backoff, scores and temporary bans | A network adversary controlling diverse addresses can still bias reachability; production seeds and topology diversity remain required |
| Queue and request exhaustion | Per-peer read/write/frame budgets, bounded send queues, in-flight request caps, validation-worker caps, one active download lease | Correct limits require deployment measurement under expected latency |
| Expensive invalid Orchard traffic | Canonical decoding before verification, bounded verifier jobs, peer penalties for invalid proofs, disconnect threshold | A distributed sender set can consume the configured verifier budget |
| Duplicate or conflicting transaction relay | Canonical transaction IDs, atomic multi-transaction admission, mempool conflict checks, bounded relay pool, rollback on partial failure | Transaction-origin timing remains observable |
| Invalid or unsolicited block data | Headers-first validation, cumulative-work comparison, download ownership, request IDs, chunk manifests, transaction-ID checks, full block validation before activation | Long valid competing branches remain computationally expensive |
| Partial or conflicting chunks | Size/count/sequence limits, exact duplicate idempotence, conflicting duplicate rejection, checksummed checkpoints | Checkpoint availability does not authenticate a peer |
| Miner forgery or replay | Node-owned jobs, fixed job identifier, stale/unknown/replay rejection, CPU KawPoW recomputation | The initial mining endpoint is loopback-only and is not a public service |
| Crash between block and shielded commits | Checksummed write-ahead intent, ordered fsync operations, startup reconciliation, connecting-branch revalidation, atomic shielded-state replacement | Filesystem or storage hardware that violates durability guarantees can defeat the protocol |
| Reorganization state split | Candidate-chain planning, staged shielded disconnect/connect, validation before mutation, journal recovery from durable active tip | Mempool revalidation after every production tip change must remain wired into node assembly |
| Corrupt peer or state database | Bounded checksummed formats and fail-closed restore | Recovery may require operator intervention; silent repair is not attempted |
| Sensitive logging | Structured status records contain identifiers, counts, sizes and latency only | Network observers can still correlate timing and addresses |

## Abuse handling

A malformed peer is disconnected without terminating unrelated sessions.
Penalties saturate and do not overflow. Local capacity failures such as relay
cache exhaustion are backpressure, not proof of remote misbehavior. Ban state
does not affect consensus and must remain bounded.

Flood tests cover per-tick frame limits, queue backpressure, handshake timeout,
malformed-frame isolation, deterministic forward progress, and clean shutdown.
ASan and UBSan run the portable test suite. These tests do not model volumetric
traffic outside the host or cloud provider.

## Privacy

TLS protects payloads in transit between configured peers. It does not hide
source and destination addresses, connection timing, byte counts, or relay
order. Stage 7 does not claim network-layer sender anonymity. Public deployment
requires a separate decision on diffusion, cover traffic, or stem/fluff relay
after measuring latency and abuse impact.

Evidence must not contain certificate private keys, wallet material, viewing
keys, plaintext private transaction bodies, or reusable proving secrets.

## Operational failures

The node must fail closed on certificate-name mismatch, unknown trust root,
expired certificate, invalid proof, inconsistent recovery journal, or
unreconcilable block/shielded tips. Operators must not bypass those failures by
turning off verification. Certificate rotation should overlap trust roots only
for a bounded maintenance window and remove the retired root afterward.

## Open validation

The remaining physical gates are:

- mutual TLS exchange between independent machines with source-restricted
  firewall rules;
- ten minutes of at least 100 unique, valid private transactions per second
  admitted and relayed across three published nodes;
- near-limit block propagation and full validation between published nodes with
  a recorded latency budget.

Loopback, deterministic-verifier, and repeated-proof throughput results are
supporting measurements. They do not close these physical gates.
