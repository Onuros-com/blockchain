#ifndef ONUROS_PRIVACY_ENGINE_V1_H
#define ONUROS_PRIVACY_ENGINE_V1_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct onuros_privacy_engine_v1 onuros_privacy_engine_v1;
typedef struct onuros_privacy_prover_v1 onuros_privacy_prover_v1;
typedef struct onuros_privacy_witness_tree_v1 onuros_privacy_witness_tree_v1;

typedef enum onuros_privacy_status_v1 {
    ONUROS_PRIVACY_OK = 0,
    ONUROS_PRIVACY_NULL_ARGUMENT = 1,
    ONUROS_PRIVACY_INVALID_UTF8 = 2,
    ONUROS_PRIVACY_IO = 3,
    ONUROS_PRIVACY_INVALID_PARAMETERS = 4,
    ONUROS_PRIVACY_INVALID_ROOT = 5,
    ONUROS_PRIVACY_INVALID_PAYMENT_ENCODING = 6,
    ONUROS_PRIVACY_UNKNOWN_ROOT_HEIGHT = 7,
    ONUROS_PRIVACY_EFFECT_DIGEST_MISMATCH = 8,
    ONUROS_PRIVACY_INVALID_PROOF = 9,
    ONUROS_PRIVACY_INTERNAL_PANIC = 10,
    ONUROS_PRIVACY_PARAMETER_IDENTITY_MISMATCH = 11,
    ONUROS_PRIVACY_ROOT_FROM_FUTURE = 12,
    ONUROS_PRIVACY_ROOT_EXPIRED = 13,
    ONUROS_PRIVACY_DUPLICATE_NULLIFIER = 14,
    ONUROS_PRIVACY_TOO_MANY_PAYMENTS = 15,
    ONUROS_PRIVACY_OUTPUT_TOO_SMALL = 16,
    ONUROS_PRIVACY_RECOVERY_FAILED = 17,
    ONUROS_PRIVACY_INVALID_WITNESS = 18,
    ONUROS_PRIVACY_ENCRYPTION_FAILED = 19,
    ONUROS_PRIVACY_PROVING_FAILED = 20,
    ONUROS_PRIVACY_WITNESS_TREE_FULL = 21,
    ONUROS_PRIVACY_WITNESS_POSITION_UNKNOWN = 22,
    ONUROS_PRIVACY_SNAPSHOT_BINDING_MISMATCH = 23,
    ONUROS_PRIVACY_SNAPSHOT_TOO_LARGE = 24,
    ONUROS_PRIVACY_ROLLBACK_TOO_DEEP = 25,
    ONUROS_PRIVACY_INVALID_CHECKPOINT = 26,
    ONUROS_PRIVACY_INVALID_SNAPSHOT = 27
} onuros_privacy_status_v1;

typedef struct onuros_accepted_root_v1 {
    uint32_t height;
    uint8_t root[32];
} onuros_accepted_root_v1;

typedef struct onuros_validated_payment_v1 {
    uint32_t root_height;
    uint64_t fee;
    uint8_t effect_digest[32];
    uint8_t nullifier[32];
    uint8_t output_commitments[2][32];
} onuros_validated_payment_v1;

typedef struct onuros_recovered_note_v1 {
    uint8_t recipient[32];
    uint64_t value;
    uint8_t randomness[32];
} onuros_recovered_note_v1;

typedef struct onuros_account_keys_v1 {
    uint8_t spending_secret[32];
    uint8_t recipient[32];
    uint8_t recovery_seed[32];
    uint8_t recovery_public_key[32];
} onuros_account_keys_v1;

typedef struct onuros_payment_note_v1 {
    uint8_t recipient[32];
    uint64_t value;
    uint8_t randomness[32];
} onuros_payment_note_v1;

typedef struct onuros_payment_construction_v1 {
    uint32_t root_height;
    uint8_t root[32];
    uint64_t fee;
    onuros_payment_note_v1 input;
    uint8_t input_secret[32];
    uint8_t siblings[32][32];
    uint8_t right[32];
    onuros_payment_note_v1 outputs[2];
    uint8_t output_recovery_public_keys[2][32];
} onuros_payment_construction_v1;

onuros_privacy_status_v1 onuros_privacy_recovery_public_key_v1(
    const uint8_t seed[32], uint8_t public_key[32]);

onuros_privacy_status_v1 onuros_privacy_derive_account_v1(
    const uint8_t wallet_root[32], uint32_t network_id,
    uint32_t account_index, onuros_account_keys_v1 *result);

onuros_privacy_status_v1 onuros_privacy_recover_compact_note_v1(
    const uint8_t seed[32], uint32_t network_id, uint32_t circuit_version,
    const uint8_t root[32], uint32_t output_index,
    const uint8_t commitment[32], const uint8_t compact_encrypted_note[120],
    onuros_recovered_note_v1 *result);

uint32_t onuros_privacy_engine_abi_version(void);
size_t onuros_privacy_engine_payment_bytes_v1(void);
size_t onuros_privacy_engine_max_batch_payments_v1(void);
size_t onuros_privacy_witness_tree_max_leaves_v1(void);

onuros_privacy_status_v1 onuros_privacy_witness_tree_snapshot_size_v1(
    const onuros_privacy_witness_tree_v1 *tree, size_t *snapshot_size);
onuros_privacy_status_v1 onuros_privacy_witness_tree_export_v1(
    const onuros_privacy_witness_tree_v1 *tree, uint8_t *output,
    size_t output_capacity, size_t *written);
onuros_privacy_witness_tree_v1 *onuros_privacy_witness_tree_import_v1(
    const uint8_t *encoded, size_t encoded_length,
    onuros_privacy_status_v1 *status);
onuros_privacy_witness_tree_v1 *onuros_privacy_witness_tree_open_v1(void);
void onuros_privacy_witness_tree_close_v1(onuros_privacy_witness_tree_v1 *tree);
onuros_privacy_status_v1 onuros_privacy_witness_tree_len_v1(
    const onuros_privacy_witness_tree_v1 *tree, uint64_t *leaf_count);
onuros_privacy_status_v1 onuros_privacy_witness_tree_append_v1(
    onuros_privacy_witness_tree_v1 *tree, const uint8_t commitment[32],
    uint64_t *position);
onuros_privacy_status_v1 onuros_privacy_witness_tree_truncate_v1(
    onuros_privacy_witness_tree_v1 *tree, uint64_t leaf_count);
onuros_privacy_status_v1 onuros_privacy_witness_tree_root_v1(
    onuros_privacy_witness_tree_v1 *tree, uint8_t root[32]);
onuros_privacy_status_v1 onuros_privacy_witness_tree_witness_v1(
    onuros_privacy_witness_tree_v1 *tree, uint64_t position,
    uint8_t root[32], uint8_t siblings[32][32], uint8_t right[32]);

onuros_privacy_prover_v1 *onuros_privacy_prover_open_v1(
    const char *parameter_path, uint32_t network_id,
    uint32_t circuit_version, const uint8_t expected_parameter_sha256[32],
    onuros_privacy_status_v1 *status);
void onuros_privacy_prover_close_v1(onuros_privacy_prover_v1 *prover);
onuros_privacy_status_v1 onuros_privacy_construct_payment_v1(
    const onuros_privacy_prover_v1 *prover,
    const onuros_payment_construction_v1 *request, uint8_t *payment,
    size_t payment_capacity);

onuros_privacy_engine_v1 *onuros_privacy_engine_open_v1(
    const char *parameter_path, uint32_t network_id,
    uint32_t circuit_version, uint32_t root_height, const uint8_t root[32],
    const uint8_t expected_parameter_sha256[32],
    onuros_privacy_status_v1 *status);
onuros_privacy_engine_v1 *onuros_privacy_engine_open_roots_v1(
    const char *parameter_path, uint32_t network_id,
    uint32_t circuit_version, const onuros_accepted_root_v1 *roots,
    size_t root_count, const uint8_t expected_parameter_sha256[32],
    onuros_privacy_status_v1 *status);
void onuros_privacy_engine_close_v1(onuros_privacy_engine_v1 *engine);
onuros_privacy_status_v1 onuros_privacy_engine_validate_payment_v1(
    const onuros_privacy_engine_v1 *engine, const uint8_t *payment,
    size_t payment_length, onuros_validated_payment_v1 *result);
onuros_privacy_status_v1 onuros_privacy_engine_validate_payment_at_height_v1(
    const onuros_privacy_engine_v1 *engine, const uint8_t *payment,
    size_t payment_length, uint32_t chain_height, uint32_t max_root_age,
    onuros_validated_payment_v1 *result);

onuros_privacy_status_v1 onuros_privacy_engine_validate_payment_with_roots_at_height_v1(
    const onuros_privacy_engine_v1 *engine,
    const uint8_t *payment,
    size_t payment_length,
    const onuros_accepted_root_v1 *roots,
    size_t root_count,
    uint32_t chain_height,
    uint32_t max_root_age,
    onuros_validated_payment_v1 *result);

onuros_privacy_status_v1 onuros_privacy_engine_validate_batch_at_height_v1(
    const onuros_privacy_engine_v1 *engine, const uint8_t *payments,
    size_t payment_count, uint32_t chain_height, uint32_t max_root_age,
    onuros_validated_payment_v1 *results, size_t result_capacity);
onuros_privacy_status_v1 onuros_privacy_engine_validate_batch_with_roots_at_height_v1(
    const onuros_privacy_engine_v1 *engine, const uint8_t *payments,
    size_t payment_count, const onuros_accepted_root_v1 *roots,
    size_t root_count, uint32_t chain_height, uint32_t max_root_age,
    onuros_validated_payment_v1 *results, size_t result_capacity);

#ifdef __cplusplus
}
#endif

#endif
