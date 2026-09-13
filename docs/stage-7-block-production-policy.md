# Stage 7 block-production budget

The 16 MiB consensus decoder ceiling remains a denial-of-service safety bound.
It is not the sustained production target.

## Policy model

`RollingBlockProductionBudget` applies two limits:

- `burst_bytes` is the largest individual block a producer may select and can
  never exceed the 16 MiB decoder ceiling;
- `sustained_bytes * rolling_window_blocks` is the maximum encoded total over
  the rolling window.

A burst therefore consumes capacity that must be recovered by smaller later
blocks. The window calculation includes the candidate block and removes the
oldest observation only when the window is already full. Overflow and invalid
parameter combinations fail closed. The policy is disabled unless explicitly
activated.

The helper `projected_archival_bytes_per_year` reports the upper-bound annual
block bytes implied by a sustained budget and block interval. For scale only,
a 4 MiB sustained budget at one block per 60 seconds is 2,204,526,182,400
bytes per 365-day year before database indexes and filesystem overhead. This is
an example, not an activation parameter.

## Activation blocker

No sustained or burst values are selected in this checkpoint. The current
two-action private transaction envelope is 9,173 bytes. At 100 transactions per
second and a 60-second block interval, transaction envelopes alone require
55,038,000 bytes per block. That cannot fit either a 4 MiB operational target
or the 16 MiB absolute safety ceiling.

Activation therefore waits for one of these reviewed decisions:

1. proof aggregation or a different proof representation that preserves the
   security boundary;
2. a lower Layer-1 settlement rate with separately stated relay and execution
   throughput;
3. another measured design approved as a roadmap change.

After that decision, candidate sustained and burst values require physical
propagation, validation, reorganization-memory and archival-growth evidence.
Only then should fixed testnet activation parameters enter consensus code.
