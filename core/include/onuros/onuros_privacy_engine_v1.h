#ifndef ONUROS_PRIVACY_ENGINE_V1_H
#define ONUROS_PRIVACY_ENGINE_V1_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct onuros_privacy_engine_v1 onuros_privacy_engine_v1;

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

onuros_privacy_status_v1 onuros_privacy_engine_validate_payment_with_roots_at_height_v1(
    const onuros_privacy_engine_v1 *engine,
    const uint8_t *payment,
    size_t payment_length,
    const onuros_accepted_root_v1 *roots,
    size_t root_count,
    uint32_t chain_height,
    uint32_t max_root_age,
    onuros_validated_payment_v1 *result);

#ifdef __cplusplus
}
#endif

#endif
