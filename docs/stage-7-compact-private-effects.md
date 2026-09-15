# Stage 7 compact private effects prototype

> Historical Orchard/Halo2 document. Superseded for active protocol use;
> `active_privacy_protocol_qualified=false`. Retained for traceability.

This non-activated prototype measures the byte floor for private state changes
when transaction authorization is supplied by a future reviewed block proof.
It does not define a spendable transaction, bypass Orchard verification, or
change the 16 MiB block safety ceiling.

Each action preserves five 32-byte Orchard effect fields, a fixed 68-byte
authenticated compact note ciphertext, and the 80-byte outgoing recovery
ciphertext required for sender recovery and user-controlled disclosure. The
fixed ciphertext class avoids making memo presence an immediate size
distinguisher. Optional memos require a separately authenticated and prunable
design and are outside this checkpoint.

The version-1 `ONE1` encoding is canonical, fixed-width and bounded before
allocation. It rejects unknown versions and flags, negative fees, zero or
excessive action counts, truncation and trailing bytes. A domain-separated
digest commits to the exact encoding.

For a normal two-action transfer:

- action effects: 308 bytes each;
- effect header: 29 bytes;
- encoded effects: 645 bytes;
- length-delimited effects: 649 bytes;
- 6,000 length-delimited effects: 3,894,000 bytes before block-level proof,
  header and commitments.

These figures are a serialization feasibility result only. Activation remains
blocked on a reviewed encryption construction, recursive authorization proof,
wallet recovery tests, reorganization binding, independent audit and physical
multi-node qualification.
