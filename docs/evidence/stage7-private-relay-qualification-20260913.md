# Stage 7 physical private-relay qualification — 2026-09-13

## Scope

This record summarizes the physical Stage 7 Gate 6 qualification performed
across three independent hosts. The private transaction corpus, TLS private
keys, and spending material are deliberately excluded.

The tested source commit was
`6662c32178542b9742e2fd0febd7e0da776d8ab5`.

## Topology

| Role | Host class | CPU threads | Result |
|---|---|---:|---|
| Origin | RTX 3060 workstation / WSL2 | 12 | PASS |
| Relay | Google Compute Engine, AMD EPYC 9B14 | 8 | PASS |
| Observer | RTX 4070 workstation / WSL2 | 20 | PASS |

All roles authenticated with certificates issued by the same dedicated Stage 7
private CA and negotiated `TLS_AES_256_GCM_SHA384`.

## Workload

- Generated transactions: 66,000 distinct valid Orchard private transactions.
- Corpus bytes: 605,154,044.
- Corpus SHA-256:
  `4fd87754412ff338d288ff435403119772cdffc6e206d27d32f6c57918dac996`.
- Corpus generation and merge result: `PASS`.
- Corpus publication: forbidden; the corpus contains private test transaction
  bodies.
- Spending keys in corpus or evidence: false.
- Private payloads logged: false.

## Qualification measurements

| Measurement | Origin | Relay | Observer |
|---|---:|---:|---:|
| Duration, seconds | 659.241079 | 659.104238 | 658.933599 |
| Submitted unique | 66,000 | 66,000 | 66,000 |
| Admitted unique | 66,000 | 66,000 | 66,000 |
| Relayed unique | 66,000 | 66,000 | 66,000 |
| Admitted TPS | 100.115120 | 100.135906 | 100.161837 |
| Verification workers | 8 | 8 | 8 |
| Verification tasks | 66,000 | 66,000 | 66,000 |
| Verification batches | 4,125 | 4,125 | 4,125 |
| Verification seconds | 443.283617 | 600.853389 | 237.766406 |
| Queue high watermark / limit | 16 / 32 | 16 / 32 | 16 / 32 |
| Duplicate transactions | 0 | 0 | 0 |
| Invalid transactions | 0 | 0 | 0 |
| Divergent transactions | 0 | 0 | 0 |
| Limits exceeded | 0 | 0 | 0 |
| Process exit status | 0 | 0 | 0 |

All three roles recorded the same transaction-set SHA-256:

`f02493d6fbf7c6b53d42716dda7e970738536f69141c4b2f2c404d6785cf8d76`

First transaction ID:

`c577ad4346d6c6825d682273ba2a76787ac03415f83c2d1de3f8aeed16a05a04`

Last transaction ID:

`be111003f2fe95a8932eea3999ea3ebafd65efc892cc0436dfd111685dd19986`

The repository validator reported:

```text
stage7_unique_relay_gate=PASS nodes=3 minimum_tps=100 minimum_seconds=600 id_set_sha256=f02493d6fbf7c6b53d42716dda7e970738536f69141c4b2f2c404d6785cf8d76
```

## Evidence integrity

The origin and relay evidence captures passed
`capture-stage7-private-load-evidence.sh`, included no private keys, and
recorded the same CA certificate SHA-256:

`5b328593f6ad5ee6def7713e059440947599219d3aa208ebc13f7a210ef4914d`

Captured-file hashes:

| Evidence | SHA-256 |
|---|---|
| Origin node manifest | `c36b7f50a94b3e9eed391f9e3ca38716022b4623d479e3c191078241de8cbfe3` |
| Origin node log | `1f205c003cdf387d26efe31f91a7221c4601401ab0d374fa373737c73ddcdcb2` |
| Relay node manifest | `cabce72d829b7ff1c45b9f8d08a8029bc56648ffcff9002327291f2fc149ef20` |
| Relay node log | `8f23230ea4b8b26cc327997be7f2eca61d71b0b108052fca975bb85b0565f839` |
| Observer raw evidence archive | `4fe96ad374704b62cd1da470e6e345891e7def595cfc791dc121c55ea8770437` |
| Origin evidence transfer archive | `84d9b4e06dd0143152d93a9a1e386f52289f319f6a0fd7f66a7c0ba131905dce` |
| Gate 6 checkpoint archive | `1bc98918a1d31589ff6f81cc74a0f1c8fbf3fa59901e60f1c208d04a795b4150` |

The Gate 6 checkpoint archive was verified on both the relay and origin hosts.

## Remaining audit action

The numerical three-node Gate 6 validator has passed. The observer manifest and
log are preserved and hash-bound in the transferred observer archive. The
observer host metadata capture remains pending because the RTX 4070 was
unavailable after the run. When that host is next available:

1. Run `capture-stage7-private-load-evidence.sh` against the preserved
   observer manifest and log.
2. Transfer the sanitized observer evidence directory to the audit host.
3. Run `validate-stage7-evidence-bundle.sh relay` across the origin, relay,
   and observer directories.
4. Attach the validator output and update this record.

The 600-second load run does not need to be repeated.
