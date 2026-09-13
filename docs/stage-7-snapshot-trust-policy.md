# Stage 7 snapshot trust policy

Authenticated snapshots are an optional synchronization accelerator. They do
not change proof of work, chain selection, finality, transaction validity or
the shielded root. A node that cannot establish the configured signature
threshold must ignore the snapshot and synchronize from independently verified
headers and blocks.

## Thresholds

- Testnet: 2-of-3 Ed25519 signatures.
- Mainnet: 3-of-5 Ed25519 signatures.
- Every accepted threshold must contain at least one signature from a key
  designated as an offline security key.
- Repeated signer identifiers, repeated public keys, unknown signers, invalid
  signatures and malformed policies fail closed.

Testnet's three roles are the release/build signer, the founder's offline
security signer and an independent recovery signer. Mainnet snapshot trust must
remain disabled until five keys have separate named custodians, at least two
keys are kept offline, and a 3-of-5 recovery drill has passed. Distinct key
files controlled by one person do not count as independent custody.

## Signed message

Each signer signs the Ed25519 message consisting of the fixed Onuros snapshot
signature domain followed by the double-SHA-256 manifest identifier. The
manifest already binds the chain-bound pruning checkpoint, consensus-parameter
hash, state-file length and state-file hash. Domain separation prevents a
signature from being reused as approval of another Onuros object type.

## Late-node recovery

Before atomic import, a late node must:

1. verify the configured threshold and offline-security participation;
2. validate the manifest checksum and exact authenticated identifier;
3. match genesis, active tip, height, cumulative work and shielded root against
   an independently verified header chain;
4. match the compiled consensus-parameter hash;
5. verify the complete state-file length, hash and internal checksum;
6. open the imported shielded state and continue ordinary block validation from
   that exact tip.

Any mismatch leaves the existing destination unchanged. Snapshot signing keys
cannot authorize transactions, decrypt notes, select a chain or override proof
of work.
