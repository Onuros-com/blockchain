#ifndef ONUROS_PRIVACY_TRACKED_WITNESS_V2_H
#define ONUROS_PRIVACY_TRACKED_WITNESS_V2_H

#include "onuros_privacy_engine_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct onuros_privacy_tracked_witness_tree_v2
    onuros_privacy_tracked_witness_tree_v2;
typedef struct onuros_privacy_tracked_witness_checkpoint_v2
    onuros_privacy_tracked_witness_checkpoint_v2;

typedef struct onuros_tracked_witness_binding_v2 {
    uint32_t network_id;
    uint32_t circuit_version;
    uint64_t block_height;
    uint8_t block_hash[32];
    uint8_t note_root[32];
} onuros_tracked_witness_binding_v2;

typedef struct onuros_tracked_witness_limits_v2 {
    size_t max_snapshot_bytes;
    size_t max_tracked_positions;
    size_t max_retained_nodes;
    uint64_t max_rollback_leaves;
} onuros_tracked_witness_limits_v2;

uint32_t onuros_privacy_tracked_witness_abi_version_v2(void);
onuros_privacy_tracked_witness_tree_v2 *
onuros_privacy_tracked_witness_open_v2(
    uint64_t max_rollback_leaves, onuros_privacy_status_v1 *status);
void onuros_privacy_tracked_witness_close_v2(
    onuros_privacy_tracked_witness_tree_v2 *tree);
onuros_privacy_status_v1 onuros_privacy_tracked_witness_len_v2(
    const onuros_privacy_tracked_witness_tree_v2 *tree,
    uint64_t *leaf_count, uint64_t *tracked_count);
onuros_privacy_status_v1 onuros_privacy_tracked_witness_append_v2(
    onuros_privacy_tracked_witness_tree_v2 *tree,
    const uint8_t commitment[32], uint8_t track, uint64_t *position);
onuros_privacy_status_v1 onuros_privacy_tracked_witness_untrack_v2(
    onuros_privacy_tracked_witness_tree_v2 *tree, uint64_t position);
onuros_privacy_status_v1 onuros_privacy_tracked_witness_root_v2(
    const onuros_privacy_tracked_witness_tree_v2 *tree, uint8_t root[32]);
onuros_privacy_status_v1 onuros_privacy_tracked_witness_path_v2(
    const onuros_privacy_tracked_witness_tree_v2 *tree, uint64_t position,
    uint8_t root[32], uint8_t siblings[32][32], uint8_t right[32]);
onuros_privacy_tracked_witness_checkpoint_v2 *
onuros_privacy_tracked_witness_checkpoint_open_v2(
    const onuros_privacy_tracked_witness_tree_v2 *tree,
    onuros_privacy_status_v1 *status);
void onuros_privacy_tracked_witness_checkpoint_close_v2(
    onuros_privacy_tracked_witness_checkpoint_v2 *checkpoint);
onuros_privacy_status_v1 onuros_privacy_tracked_witness_rollback_v2(
    onuros_privacy_tracked_witness_tree_v2 *tree,
    const onuros_privacy_tracked_witness_checkpoint_v2 *checkpoint);
onuros_privacy_status_v1 onuros_privacy_tracked_witness_snapshot_size_v2(
    const onuros_privacy_tracked_witness_tree_v2 *tree,
    const onuros_tracked_witness_binding_v2 *binding, size_t *snapshot_size);
onuros_privacy_status_v1 onuros_privacy_tracked_witness_export_v2(
    const onuros_privacy_tracked_witness_tree_v2 *tree,
    const onuros_tracked_witness_binding_v2 *binding, uint8_t *output,
    size_t output_capacity, size_t *written);
onuros_privacy_tracked_witness_tree_v2 *
onuros_privacy_tracked_witness_import_v2(
    const uint8_t *encoded, size_t encoded_length,
    const onuros_tracked_witness_binding_v2 *expected_binding,
    const onuros_tracked_witness_limits_v2 *limits,
    onuros_privacy_status_v1 *status);

#ifdef __cplusplus
}
#endif

#endif
