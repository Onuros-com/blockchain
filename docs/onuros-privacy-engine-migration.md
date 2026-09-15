# Onuros Privacy Engine migration qualification

Status: integration in progress; consensus activation and production claims
remain disabled.

## Pinned interface boundary

- Complete private payment: 584 bytes.
- Payment validation interface: Onuros Privacy Engine ABI v1.
- Scalable tracked-witness interface: ABI v2.
- The ABI v2 snapshot binding contains network, circuit, block height, block
  hash and note root. The binding must come from an authenticated header-chain
  view; a snapshot checksum is not chain authority.
- Legacy Orchard support remains isolated until replacement parity, physical
  qualification and review are complete.

The tracked-witness tree is wallet/recovery state. It does not replace the
consensus nullifier set, note-root commitment, block admission or durable
node-state journal.

## Integration acceptance sequence

1. Link the pinned Privacy Engine and reject ABI, parameter, circuit or payment
   encoding mismatches before admission.
2. Admit genuine 584-byte payments through the normal mempool and block paths;
   reject malformed encodings, invalid proofs, stale/unknown roots, duplicate
   nullifiers and mismatched effects without partial state changes.
3. Bind tracked-witness exports to the authenticated chain view, enforce import
   allocation limits, and prove checkpoint/rollback and corrupt/cross-chain
   snapshot rejection.
4. Run candidate block commit, restart, rollback and competing-branch reorg
   parity against the durable node state.
5. Close the genesis-synchronization gap explicitly: start fresh independent
   hosts from one genesis, start a late node, record one active tip and candidate
   note root, then reopen a stopped client from its existing database. A local
   loopback run is regression evidence only.
6. Rerun the sustained 100-payment/s, 600-second qualification and near-capacity
   block test with the active candidate. Orchard measurements remain historical
   evidence.

## Genesis-synchronization evidence contract

Final candidate evidence must bind all hosts to one immutable Blockchain commit,
Privacy Lab commit, Privacy Engine binary hash, parameter hash, circuit version,
network identifier and TLS CA. Each manifest must record:

- `qualification_profile=onuros-private-payment-v1`;
- `active_privacy_protocol_qualified=true`;
- Privacy Engine ABI 1 and tracked-witness ABI 2;
- the same genesis block identifier;
- the same active height, tip and candidate note root;
- successful local candidate validation and durable activation;
- a late-node synchronization result; and
- an offline restart that recovers the initial client's existing state.

The existing `transport-fixture-v1` manifest and validator intentionally record
`active_privacy_protocol_qualified=false`. They validate the independent-host
transport machinery but cannot satisfy this candidate evidence contract.
