# Stage 7 private transaction byte accounting

> Historical Orchard/Halo2 document. Superseded for active protocol use;
> `active_privacy_protocol_qualified=false`. Retained for traceability.

This report describes canonical format version 2. It does not authorize a
format change or remove any Orchard field.

## Canonical layout

| Component | One action | Two actions |
|---|---:|---:|
| Bundle magic | 4 | 4 |
| Bundle format version | 4 | 4 |
| Proof-system version | 4 | 4 |
| Orchard flags | 1 | 1 |
| Anchor | 32 | 32 |
| Value balance | 8 | 8 |
| Fee | 8 | 8 |
| Action count | 4 | 4 |
| Action core fields | 160 | 320 |
| Encrypted notes | 580 | 1,160 |
| Outgoing ciphertexts | 80 | 160 |
| Spend authorizations | 64 | 128 |
| Proof length | 4 | 4 |
| Orchard proof | 4,992 | 7,264 |
| Binding signature | 64 | 64 |
| **Private body** | **6,009** | **9,165** |
| Transaction envelope | 6,017 | 9,173 |
| Single-transaction network batch | 6,021 | 9,177 |

The two-action form is the current spend-and-output benchmark transaction. Its
9,165-byte body consists of 7,264 proof bytes (79.26%), 1,320 encrypted-note
and outgoing-ciphertext bytes (14.40%), 320 action-core bytes (3.49%), 192
signature bytes (2.09%), and 69 bytes of fixed header and proof-length data
(0.75%).

`onuros_stage7_private_byte_accounting` emits the same values from constants
used by the encoder. `private_transaction_test` verifies one- and two-action
sizes, round-trip canonical encoding, every truncation boundary, trailing-byte
rejection and this two-action transaction-ID vector:

```text
24ae52efbcccf272a67253ca8e9a0ce9c5d4651b3242b0d0d05567eb3d628a51
```

The vector runs in the Linux, Windows and sanitizer jobs. A disagreement is a
format failure, not a platform-specific allowance.

## Reduction boundary

At 6,000 two-action transactions per 60-second block, canonical transaction
envelopes alone occupy 55,038,000 bytes before the block header and transaction
count. A 4 MiB block budget allows approximately 699 bytes per transaction.

No safe serialization-only change can meet that budget. The Orchard proof by
itself is 7,264 bytes for the two-action form. Removing every Onuros fixed
header byte would save less than one percent and would still leave the block
more than twelve times over the target.

The following fields are not candidates for unreviewed removal:

- nullifier, note commitment, value commitment and randomized key;
- anchor and value balance;
- encrypted note and outgoing recovery ciphertext;
- spend and binding signatures;
- Orchard proof bytes.

Transport batching can remove repeated network framing and scheduling cost,
but it does not reduce consensus block bytes. Reaching approximately 4 MiB at
100 settled private TPS therefore requires reviewed proof aggregation, a
materially different proof representation, or a lower settled TPS/block
policy. That decision remains outside this accounting checkpoint.
