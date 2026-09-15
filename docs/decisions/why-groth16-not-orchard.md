# Why Groth16/Poseidon replaced Orchard/Halo2

Date: 2026-09-15  
Status: active testnet engineering decision; not a mainnet security approval.

Onuros selected the Privacy Lab Groth16/BLS12-381 + Poseidon candidate because
it materially reduces private-payment bytes and increased measured verification
headroom. The candidate uses a 192-byte proof and a complete 584-byte
one-input/two-output payment. The historical Orchard layout used a 4,992-byte
proof for one action and 7,264 proof bytes for two actions. In the Privacy Lab's
6,000-unique-payment checkpoint, individual validation took 13.177813 seconds
(455.3108 payments/s) and randomized batch validation had a 2.587458-second
median (2,318.8782 payments/s). The earlier Orchard duration checkpoint recorded
72,135 repeated-fixture verifications in 600.033 seconds (120.218/s). These runs
used different hosts and workloads, so the speed figures are directional—not a
controlled hardware-matched comparison.

The accepted tradeoff is Groth16's circuit-specific trusted setup. No mainnet
release is permitted until the circuit and encodings are frozen, an independent
constraint-level audit is complete, and a public multi-party ceremony produces
a reproducible transcript and activation-pinned verifying-key identity. A
circuit-incompatible change requires a new protocol version and ceremony.

Orchard/Halo2 code and results remain as labeled historical material. They are
not active defaults and were not deleted to make the new measurements look
better.
