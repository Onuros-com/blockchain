# Stage 7 effect and authorization commitments

> Historical Orchard/Halo2 document. Superseded for active protocol use;
> `active_privacy_protocol_qualified=false`. Retained for traceability.

This scalability checkpoint defines a research-only version-1 commitment split
for the existing canonical `ONP2` private transaction. It does not activate a
new transaction format, change Orchard verification, or alter the 16 MiB block
safety ceiling.

`private_effect_digest` commits to the transaction and commitment-scheme
versions, flags, value balance, fee, action count, value commitments,
nullifiers, randomized keys, note commitments, ephemeral keys, encrypted notes
and outgoing recovery ciphertexts. It deliberately excludes the anchor, proof
and signatures.

`private_authorizing_data_commitment` commits to the effect digest, anchor,
action count, spend-authorization signatures, length-delimited Orchard proof
and binding signature. Embedding the effect digest prevents valid authorization
data from being transplanted onto different effects.

`private_effect_authorization_commitment` domain-separates and commits to the
ordered pair. A future reviewed block format may use this split to retain
state-changing and wallet-recovery data while replacing per-transaction
authorization payloads with a block proof. No such replacement is enabled by
this checkpoint.

All three hashes use distinct ASCII domains, an explicit scheme version and
double SHA-256. Unsupported scheme versions fail closed. The byte-accounting
executable publishes deterministic zero-filled vectors, and the private
transaction tests cover effect mutations, authorization mutations, transplant
resistance and version rejection.
