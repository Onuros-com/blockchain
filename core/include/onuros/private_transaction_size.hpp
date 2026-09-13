#pragma once

#include "onuros/block_format.hpp"
#include "onuros/private_transaction.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace onuros {

// Canonical ONP2 byte layout. Keep these values derived from the encoded
// fields so size policy and reporting cannot drift away from serialization.
inline constexpr std::size_t private_bundle_magic_bytes = 4U;
inline constexpr std::size_t private_bundle_version_bytes = 4U;
inline constexpr std::size_t private_proof_version_bytes = 4U;
inline constexpr std::size_t private_flags_bytes = 1U;
inline constexpr std::size_t private_anchor_bytes = 32U;
inline constexpr std::size_t private_value_balance_bytes = sizeof(Amount);
inline constexpr std::size_t private_fee_bytes = sizeof(Amount);
inline constexpr std::size_t private_action_count_bytes = 4U;
inline constexpr std::size_t private_proof_length_bytes = 4U;
inline constexpr std::size_t private_binding_signature_bytes = 64U;
inline constexpr std::size_t transaction_envelope_bytes = 8U;

inline constexpr std::size_t private_bundle_prefix_bytes =
    private_bundle_magic_bytes + private_bundle_version_bytes +
    private_proof_version_bytes + private_flags_bytes + private_anchor_bytes +
    private_value_balance_bytes + private_fee_bytes +
    private_action_count_bytes;

inline constexpr std::size_t private_action_effect_bytes = 5U * 32U;
inline constexpr std::size_t private_action_ciphertext_bytes =
    orchard_encrypted_note_size + orchard_outgoing_ciphertext_size;
inline constexpr std::size_t private_action_authorization_bytes = 64U;
inline constexpr std::size_t private_action_encoded_bytes =
    private_action_effect_bytes + private_action_ciphertext_bytes +
    private_action_authorization_bytes;

struct PrivateTransactionSizeBreakdown {
    std::size_t outer_framing = transaction_envelope_bytes;
    std::size_t bundle_prefix = private_bundle_prefix_bytes;
    std::size_t action_effects = 0U;
    std::size_t ciphertext = 0U;
    std::size_t action_authorization = 0U;
    std::size_t proof_framing = private_proof_length_bytes;
    std::size_t proof = 0U;
    std::size_t binding_signature = private_binding_signature_bytes;

    constexpr std::size_t authorization() const noexcept {
        return action_authorization + proof_framing + proof +
               binding_signature;
    }

    // Bytes that remain even in the hypothetical case where all proof and
    // signature material is aggregated outside each transaction. This is a
    // useful lower bound, not a valid alternative transaction encoding.
    constexpr std::size_t effect_and_ciphertext_body() const noexcept {
        return bundle_prefix + action_effects + ciphertext;
    }

    constexpr std::size_t effect_and_ciphertext_transaction() const noexcept {
        return outer_framing + effect_and_ciphertext_body();
    }

    constexpr std::size_t body() const noexcept {
        return bundle_prefix + action_effects + ciphertext + authorization();
    }

    constexpr std::size_t transaction() const noexcept {
        return outer_framing + body();
    }
};

inline std::optional<PrivateTransactionSizeBreakdown>
private_transaction_size(std::size_t action_count,
                         std::size_t proof_bytes) noexcept {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (action_count > maximum / private_action_effect_bytes ||
        action_count > maximum / private_action_ciphertext_bytes ||
        action_count > maximum / private_action_authorization_bytes)
        return std::nullopt;

    PrivateTransactionSizeBreakdown result;
    result.action_effects = action_count * private_action_effect_bytes;
    result.ciphertext = action_count * private_action_ciphertext_bytes;
    result.action_authorization =
        action_count * private_action_authorization_bytes;
    result.proof = proof_bytes;

    const auto fixed = result.outer_framing + result.bundle_prefix +
                       result.proof_framing + result.binding_signature;
    if (result.action_effects > maximum - fixed)
        return std::nullopt;
    auto total = fixed + result.action_effects;
    if (result.ciphertext > maximum - total)
        return std::nullopt;
    total += result.ciphertext;
    if (result.action_authorization > maximum - total)
        return std::nullopt;
    total += result.action_authorization;
    if (result.proof > maximum - total)
        return std::nullopt;
    return result;
}

inline std::optional<PrivateTransactionSizeBreakdown>
private_transaction_size(const PrivateTransactionBundle& bundle) noexcept {
    return private_transaction_size(bundle.actions.size(), bundle.proof.size());
}

struct PrivateBlockSizeProjection {
    std::uint32_t transactions_per_second = 0U;
    std::uint32_t block_interval_seconds = 0U;
    std::size_t transaction_count = 0U;
    std::size_t serialized_block_bytes = 0U;
};

inline std::optional<PrivateBlockSizeProjection>
project_private_block_size(std::uint32_t transactions_per_second,
                           std::uint32_t block_interval_seconds,
                           std::size_t encoded_transaction_bytes) noexcept {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    const auto rate = static_cast<std::size_t>(transactions_per_second);
    const auto interval = static_cast<std::size_t>(block_interval_seconds);
    if (interval != 0U && rate > maximum / interval)
        return std::nullopt;
    const auto count = rate * interval;
    if (encoded_transaction_bytes != 0U &&
        count > (maximum - block_prefix_encoded_size) /
                    encoded_transaction_bytes)
        return std::nullopt;
    return PrivateBlockSizeProjection{
        transactions_per_second, block_interval_seconds, count,
        block_prefix_encoded_size + count * encoded_transaction_bytes};
}

} // namespace onuros
