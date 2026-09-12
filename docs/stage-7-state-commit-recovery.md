# Stage 7 private-state commit recovery

## Scope

`PrivateNodeCommitCoordinator` coordinates a linear active-tip block append with the corresponding shielded-state transition. It prevents a restart from exposing a durable block tip whose Orchard nullifier and commitment state was not persisted.

Genesis initialization and active-chain reorganizations are outside this coordinator. Callers must not route side-branch blocks through `submit`.

## Durable files

| File | Write method | Role |
|---|---|---|
| Block store | append and file sync | Canonical block bytes and chain work |
| Shielded state | temporary file, file sync, atomic replace | Active root, nullifiers, commitments, undo history |
| Commit journal | temporary file, file sync, atomic replace | One pending block and its adjusted validation time |

The journal is bounded by the configured byte limit. Its record contains an eight-byte format marker, adjusted time, canonical block length, canonical block bytes, and a double-SHA-256 checksum.

## Commit order

1. Reject genesis, non-tip parents, or disagreement between the active block tip and shielded tip.
2. Run private block validation against the current shielded state.
3. Persist the commit journal.
4. Submit and sync the block through `LocalNode`.
5. Persist the shielded-state transition.
6. Remove the journal and sync its parent directory.

A new submission is rejected with `recovery_required` while a journal exists.

## Startup recovery

Recovery must run after both stores open and before peer traffic or block production starts.

| Journal state | Block store | Shielded state | Action |
|---|---|---|---|
| absent | any valid state | any valid state | no action |
| valid | block absent | unchanged | discard the uncommitted intent |
| valid | exact block is active tip | parent is shielded tip | revalidate and persist the shielded transition |
| valid | exact block is active tip | block is shielded tip with matching root | clear the completed intent |
| corrupt or oversized | any | any | fail closed and retain the journal |
| valid | different active tip, payload, parent, or root | mismatch | fail closed and retain the journal |

Recovery re-runs private proof, anchor, nullifier, commitment, root, and reward validation. A durable block is not enough to authorize a shielded-state update.

## Operator handling

Do not delete a journal reported as corrupt or mismatched. Stop the node and retain copies of the block store, shielded-state file, journal, logs, and binary hashes. Automated deletion is limited to an intent whose block identifier is absent from the block store.

## Current limitation

Crash-safe active-chain reorganization requires a journal that records the complete disconnect/connect plan and the prepared shielded transitions for every branch block. Until that path is implemented and tested, the coordinator accepts only a direct extension of the current active tip.
