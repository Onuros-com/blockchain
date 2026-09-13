# Stage 7 authenticated shielded-state snapshots

The snapshot path separates data integrity from checkpoint authority. The node
does not treat a file checksum or a TLS connection as consensus authority.

## Manifest binding

The version-1 manifest contains:

- the complete chain-bound pruning checkpoint, including genesis, height, tip,
  cumulative work and shielded root;
- a hash of the consensus parameters used by the exporting node;
- the exact shielded-state file size and double-SHA-256 content hash;
- a checksum covering the canonical manifest encoding.

The manifest ID is the double-SHA-256 hash of that complete encoding. The
configured Ed25519 threshold authenticates this ID independently. Snapshot
transport remains untrusted and may use any mirror.

## Import order

`import_authenticated_shielded_snapshot` performs all checks before replacing
the destination:

1. decode the canonical manifest and verify its checksum;
2. compare its ID with the independently authenticated manifest ID;
3. require the expected chain checkpoint and consensus-parameter hash;
4. require the declared content size and content hash;
5. decode the shielded-state database and match genesis, tip and root;
6. write, fsync and atomically rename the verified state.

Any failure leaves the existing destination unchanged. A successfully imported
state is reopened through `PersistentShieldedState` before use.

## Authority boundary

The selected policy is 2-of-3 for testnet and 3-of-5 for mainnet, with at least
one offline security-key signature in every accepted threshold. Exact rules,
custody requirements and the mainnet activation block are defined in
[Stage 7 snapshot trust policy](stage-7-snapshot-trust-policy.md). Supplying an
unauthenticated ID obtained beside the snapshot provides integrity only and
must not be presented as authenticated synchronization.

The deterministic late-node test generates independent Ed25519 keys, rejects
insufficient, online-only, unknown, duplicate and altered signatures, imports
only after a valid threshold, restarts at the exact tip/root and validates the
next shielded-state transition. The remaining physical exit test is an
independent machine importing a snapshot whose manifest ID was transferred
over the chosen authenticated channel, then following headers and blocks to the
same tip and shielded root.
