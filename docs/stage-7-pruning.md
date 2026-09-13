# Stage 7 conditional pruning

Pruning is disabled by default. This checkpoint defines the chain-bound safety
horizon that must exist before historical block bodies can be compacted. It
does not delete block data.

## Retention rule

A pruning policy contains two independent depths:

- `finality_depth`: the minimum age before a body is eligible;
- `reorganization_window`: the full-block history retained for permitted
  reorganizations.

Both values must be nonzero. At active height `H`, the node retains the newest
`max(finality_depth, reorganization_window)` blocks. The first retained height
is:

```text
prune_below = H + 1 - max(finality_depth, reorganization_window)
```

when the chain is deep enough. Only non-genesis bodies below that height may be
considered. Headers, parent links, cumulative work and the genesis record are
never covered by this eligibility rule.

## Checkpoint binding

The version-1 pruning checkpoint commits to:

- genesis block ID;
- active tip ID and height;
- active-tip cumulative work;
- shielded-state root;
- finality and reorganization depths;
- calculated pruning horizon.

The canonical encoding has a double-SHA-256 checksum. Decoding rejects a wrong
magic, wrong size, unsupported version, truncation, trailing data and every
checksum mismatch. Runtime validation also rejects a different chain, tip,
work value, shielded root, policy or calculated horizon.

These checks prevent a stale or copied checkpoint from authorizing deletion on
another chain state. They provide corruption detection and chain binding, not
authority from a trusted signer.

## Activation sequence

Historical body compaction remains blocked until the next checkpoint provides:

1. a versioned block-store representation that distinguishes retained bodies
   from header-only history;
2. atomic rewrite and restart recovery;
3. shielded undo retention covering the same reorganization window;
4. archive-node fallback for bodies older than the local window;
5. archive/pruned convergence, restart and permitted-reorganization tests;
6. retained-disk measurements and corrupt-metadata failure tests.

If any checkpoint validation fails, the node opens without pruning authority
and must not remove historical data.
