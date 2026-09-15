# Stage 7 physical completion checklist

This checklist joins the existing Gate 6 and Gate 7 runbooks into one
fail-closed completion sequence. It records what may be prepared without the
physical hosts and what must be measured on the original machines. It does not
replace either runbook or relax their pass conditions.

## Current physical status

| Gate | Status | Remaining action |
|---|---|---|
| Independent mutual TLS | Passed on GCP and RTX 4070 | Preserve committed evidence |
| Gate 6 private relay | Numerical three-host gate passed | Capture RTX 4070 host metadata and validate the immutable bundle |
| Gate 7 near-limit block | Tooling and automated regression passed | Run one deterministic block on the RTX 3060 and RTX 4070 receivers |
| Published-host genesis synchronization | Loopback process gate passed | Independent-host procedure and physical evidence remain open |
| Final audit record | Draft qualification record exists | Add final bundle output, Gate 7 evidence, reviewer findings and dispositions |

The Gate 6 load run used source commit
`6662c32178542b9742e2fd0febd7e0da776d8ab5`. Its 600-second measurement must
not be repeated merely to finish evidence packaging. The preserved observer
manifest and log are the measurement; the later host capture binds those files
to the original host, binary, certificate, CA and source commit.

## Evidence safety boundary

- Never commit or publish the Orchard corpus, corpus shards, TLS private keys,
  the CA private key, TLS transfer archives, IP addresses or user home paths.
- Transfer only role-specific certificates and keys to each host. The CA
  signing key stays on the administration host and outside evidence.
- Evidence directories contain only `node-manifest.txt`, `node.log` and
  `host-manifest.txt`.
- Run the repository bundle validator before copying evidence into the
  repository.
- Retain the original raw files and SHA-256 values until audit disposition.

## Reuse the Gate 6 corpus for Gate 7

The existing 66,000-entry corpus may be used as the Gate 7 sender corpus. The
block builder requires at least 2,800 entries, reads entries in canonical file
order, stops before the 16 MiB consensus ceiling, and rejects a result below
99% of that ceiling. Extra corpus entries are ignored. Reusing this corpus
therefore avoids generating another set of Orchard proofs without changing the
deterministic block.

Before the physical run:

1. Verify the source corpus SHA-256 against the private preparation manifest.
2. Transfer it only to the trusted GCP sender over the authenticated operator
   channel.
3. Verify the destination SHA-256 before starting the sender.
4. Use the same corpus, overlap percentage and source commit for both receiver
   runs.
5. Remove the corpus from GCP after both evidence bundles pass validation.

The private corpus is test input, not evidence.

## Finalize Gate 6

When the original RTX 4070 is available:

1. Confirm that its preserved observer manifest and log report the common
   transaction-set hash from the qualification run.
2. Run `capture-stage7-private-load-evidence.sh` locally using the original
   observer binary, certificate and CA certificate.
3. Confirm `private_keys_included=false` and
   `stage7_private_load_evidence=PASS role=observer`.
4. Transfer the sanitized observer evidence directory to the audit host.
5. Run `validate-stage7-evidence-bundle.sh relay` over the origin, relay and
   observer directories.
6. Preserve the validator output and hashes. Do not alter captured files after
   validation.

## Execute Gate 7

Follow `stage-7-block-propagation-runbook.md` with GCP as sender and the RTX
3060 and RTX 4070 as independent receivers. Both runs must use:

- one immutable source commit across all hosts;
- one TLS CA and explicitly verified peer identities;
- the same corpus and `--overlap 50`;
- fresh, single-use receiver data directories;
- the same deterministic block ID.

Each receiver must report:

- a canonical block between 99% and 100% of 16 MiB;
- nonzero compact announcement, request and response byte counts;
- the measured mempool overlap;
- full Orchard validation;
- durable block and shielded-state activation;
- successful database reopen and restart recovery;
- total timed propagation, validation, commit and reopen of no more than
  30.000 seconds.

Capture each receiver with `capture-stage7-block-evidence.sh`, transfer only
the two sanitized `final` directories, and run:

```bash
bash scripts/validate-stage7-evidence-bundle.sh block rtx3060 rtx4070
```

Any different block ID, source commit or CA fails the physical gate.

## Published-host synchronization gap

The existing `run-stage7-three-process.sh` gate proves convergence and restart
with three isolated processes on one host. The current
`onuros_stage7_network_node` client connects to `127.0.0.1` and is not an
independent-host TLS deployment harness. It must not be presented as physical
three-host synchronization evidence.

Before claiming the published-host synchronization gate, add or approve an
authenticated independent-host mode that preserves the existing handshake,
headers-first synchronization, bounded block retrieval, local validation and
restart checks. Then run it on three independently administered hosts and
record the common active tip and shielded root before and after restart.

## Completion decision

Stage 7 may be marked complete only after:

1. the final Gate 6 immutable bundle validator passes;
2. the two-receiver Gate 7 immutable bundle validator passes;
3. published-host synchronization evidence satisfies the networking
   specification, or the gate is explicitly re-scoped through reviewed change
   control;
4. evidence contains no private payloads, private keys or secret test
   material;
5. reviewer findings have resolution links and an explicit disposition.
