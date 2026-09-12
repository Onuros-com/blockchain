# Stage 7 private-state commit recovery

## Scope

`PrivateNodeCommitCoordinator` coordinates an active-chain block append with the corresponding shielded-state transition. It handles direct tip extensions and stronger-branch reorganizations. It prevents a restart from exposing a durable active block tip whose Orchard nullifier and commitment state was not persisted.

Genesis initialization is outside this coordinator. A block that does not activate a stronger tip returns `inactive_branch`; inactive side-branch storage does not mutate active shielded state.

## Durable files

| File | Write method | Role |
|---|---|---|
| Block store | append and file sync | Canonical block bytes and chain work |
| Shielded state | temporary file, file sync, atomic replace | Active root, nullifiers, commitments, undo history |
| Commit journal | temporary file, file sync, atomic replace | One pending block and its adjusted validation time |

The journal is bounded by the configured byte limit. Its record contains an eight-byte format marker, adjusted time, canonical block length, canonical block bytes, and a double-SHA-256 checksum.

## Commit order

1. Reject genesis or disagreement between the active block tip and shielded tip.
2. Copy the chain index, add the candidate, and require a stronger-tip reorganization plan.
3. Stage every shielded disconnect, then validate and stage every connecting branch block in order.
4. Persist the commit journal.
5. Submit and sync the candidate through `LocalNode`.
6. Atomically persist the planned shielded disconnect/connect transition.
7. Remove the journal and sync its parent directory.

A new submission is rejected with `recovery_required` while a journal exists.

## Startup recovery

Recovery must run after both stores open and before peer traffic or block production starts.

| Journal state | Block store | Shielded state | Action |
|---|---|---|---|
| absent | any valid state | any valid state | no action |
| valid | block absent | unchanged | discard the uncommitted intent |
| valid | exact block is active tip | older active branch | derive the chain plan, revalidate every connecting private block, and persist the shielded reorganization |
| valid | exact block is active tip | block is shielded tip with matching root | clear the completed intent |
| corrupt or oversized | any | any | fail closed and retain the journal |
| valid | different active tip, payload, parent, or root | mismatch | fail closed and retain the journal |

Recovery re-runs private proof, anchor, nullifier, commitment, root, and reward validation. A durable block is not enough to authorize a shielded-state update.

## Operator handling

Do not delete a journal reported as corrupt or mismatched. Stop the node and retain copies of the block store, shielded-state file, journal, logs, and binary hashes. Automated deletion is limited to an intent whose block identifier is absent from the block store.

## Side-branch handling

The journal stores the activating candidate. On restart, the disconnect/connect plan is derived from the shielded tip and the durable active block index. Every connecting block must already exist in the block store and is revalidated against a staged shielded branch before persistence.

Inactive side blocks may be retained by the block store but do not enter the active shielded snapshot. Resource policy for retained inactive branches remains a separate node-level limit.
