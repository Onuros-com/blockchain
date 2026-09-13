# Stage 7 conditional pruning

Pruning is disabled by default. A caller must construct and validate a
chain-bound checkpoint before requesting compaction.

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

## Durable block-store compaction

The version-3 block store records one explicit body-presence byte per block.
A retained record contains the canonical full block. A compacted record
contains the canonical 160-byte header and its validated per-block work. The
in-memory API reports a missing historical body as `archive_required`, which
is distinct from an unknown block.

Compaction builds and parses the complete replacement in memory before an
fsync-and-rename operation. A restart ignores an abandoned temporary file and
reconstructs the header/work index from the durable destination. New blocks
are appended with full bodies. The legacy version-2 full-block format remains
readable and is upgraded only by an explicit compaction call.

On restart, retained blocks receive full transaction-root and body validation.
Header-only records receive header, difficulty and proof-of-work validation.
Their transaction-root commitment is preserved, but the missing body cannot be
revalidated locally and must be obtained from an archive node when requested.

## Remaining activation sequence

Node-level pruning activation remains blocked until the next checkpoints
provide:

1. ~~shielded undo retention covering the same reorganization window;~~
2. ~~a network archive-body retrieval path for `archive_required` results;~~
3. archive/pruned convergence and permitted-reorganization tests;
4. retained-disk measurements on a representative chain.

If any checkpoint validation fails, the node opens without pruning authority
and must not remove historical data.

## Bounded shielded undo history

The version-2 shielded-state store separates permanent consensus state from
reorganization-only state. The complete nullifier set, ordered commitment
frontier and active-root history remain durable, while undo records are limited
to a nonzero configured window. Older valid anchors therefore remain available
to transaction validation. The store records the active height and the
block/root boundary below
the retained window, validates that every retained undo record forms the exact
active-chain suffix, and refuses a disconnect below that boundary.

Reducing the window is an atomic checksummed replacement. Increasing it is
rejected because discarded undo data cannot be reconstructed from the pruned
state. Restart preserves the limit and boundary, and every subsequent connect
enforces the limit before persistence.

## Archive body retrieval

Archive-capable peers advertise `p2p_service_archive_node`. A pruned node sends
`get_archive_block` with the required committed block identifier. The archive
returns the canonical block in ordered `archive_block` chunks. Each chunk binds
the block identifier, sequence, total chunk count and total encoded size; all
counts and bytes are bounded before allocation.

The receiver accepts chunks only in order and reconstructs the complete block
under the 16 MiB consensus ceiling. It then requires the exact locally stored
header and transaction-root commitment before atomically replacing the
header-only database record. Wrong blocks, inconsistent manifests, excessive
sizes, reordered chunks, malformed encodings and altered bodies fail closed.
