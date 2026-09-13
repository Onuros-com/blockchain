#pragma once

#include "onuros/private_transaction.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace onuros {

// Research-only format. It is never accepted by the ONP2 transaction decoder.
inline constexpr std::uint32_t compact_private_effect_version = 1U;
inline constexpr std::uint8_t compact_private_effect_flags = 0x01U;
inline constexpr std::size_t compact_note_ciphertext_size = 68U;
inline constexpr std::size_t compact_outgoing_ciphertext_size = 80U;
inline constexpr std::size_t compact_private_action_effect_size =
    private_action_core_size + compact_note_ciphertext_size +
    compact_outgoing_ciphertext_size;
inline constexpr std::size_t compact_private_effect_header_size =
    4U + 4U + 1U + 8U + 8U + 4U;
inline constexpr std::size_t compact_private_effect_length_size = 4U;

inline std::size_t compact_private_effect_encoded_size(std::size_t actions) {
    if (actions == 0U)
        throw std::invalid_argument("compact private effect requires an action");
    if (actions >
        (std::numeric_limits<std::size_t>::max() -
         compact_private_effect_header_size) /
            compact_private_action_effect_size)
        throw std::length_error("compact private effect exceeds size_t");
    return compact_private_effect_header_size +
           actions * compact_private_action_effect_size;
}

struct CompactPrivateActionEffect {
    Hash256 value_commitment{};
    Hash256 nullifier{};
    Hash256 randomized_key{};
    Hash256 note_commitment{};
    Hash256 ephemeral_key{};
    std::array<std::uint8_t, compact_note_ciphertext_size> encrypted_note{};
    std::array<std::uint8_t, compact_outgoing_ciphertext_size>
        outgoing_ciphertext{};
};

struct CompactPrivateEffects {
    std::uint32_t version = compact_private_effect_version;
    std::uint8_t flags = compact_private_effect_flags;
    Amount value_balance = 0;
    Amount fee = 0;
    std::vector<CompactPrivateActionEffect> actions;
};

struct CompactPrivateEffectLimits {
    std::size_t max_bytes;
    std::uint32_t max_actions;
};

enum class CompactPrivateEffectDecodeError {
    none,
    body_too_large,
    invalid_magic,
    unsupported_version,
    invalid_flags,
    invalid_fee,
    zero_actions,
    too_many_actions,
    truncated,
    trailing_bytes
};

struct CompactPrivateEffectDecodeResult {
    CompactPrivateEffectDecodeError error =
        CompactPrivateEffectDecodeError::truncated;
    std::optional<CompactPrivateEffects> effects;

    bool accepted() const {
        return error == CompactPrivateEffectDecodeError::none &&
               effects.has_value();
    }
};

namespace compact_private_detail {

inline constexpr std::array<std::uint8_t, 4> magic{
    0x4fU, 0x4eU, 0x45U, 0x31U // "ONE1"
};

inline Amount decode_amount(std::uint64_t encoded) {
    if (encoded <=
        static_cast<std::uint64_t>(std::numeric_limits<Amount>::max()))
        return static_cast<Amount>(encoded);
    const auto magnitude = (~encoded) + 1U;
    if (magnitude == (std::uint64_t{1} << 63U))
        return std::numeric_limits<Amount>::min();
    return -static_cast<Amount>(magnitude);
}

} // namespace compact_private_detail

inline std::vector<std::uint8_t> encode_compact_private_effects(
        const CompactPrivateEffects& effects) {
    if (effects.version != compact_private_effect_version)
        throw std::invalid_argument("unsupported compact effect version");
    if (effects.flags != compact_private_effect_flags)
        throw std::invalid_argument("unsupported compact effect flags");
    if (effects.fee < 0)
        throw std::invalid_argument("negative compact effect fee");
    if (effects.actions.empty())
        throw std::invalid_argument("compact effect has no actions");
    if (effects.actions.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("too many compact private actions");

    std::vector<std::uint8_t> output;
    output.reserve(compact_private_effect_encoded_size(effects.actions.size()));
    output.insert(output.end(), compact_private_detail::magic.begin(),
                  compact_private_detail::magic.end());
    private_detail::append_little(output, effects.version);
    output.push_back(effects.flags);
    private_detail::append_little(
        output, static_cast<std::uint64_t>(effects.value_balance));
    private_detail::append_little(
        output, static_cast<std::uint64_t>(effects.fee));
    private_detail::append_little(
        output, static_cast<std::uint32_t>(effects.actions.size()));
    for (const auto& action : effects.actions) {
        private_detail::append_hash(output, action.value_commitment);
        private_detail::append_hash(output, action.nullifier);
        private_detail::append_hash(output, action.randomized_key);
        private_detail::append_hash(output, action.note_commitment);
        private_detail::append_hash(output, action.ephemeral_key);
        private_detail::append_array(output, action.encrypted_note);
        private_detail::append_array(output, action.outgoing_ciphertext);
    }
    if (output.size() != compact_private_effect_encoded_size(effects.actions.size()))
        throw std::logic_error("compact private effect layout mismatch");
    return output;
}

inline CompactPrivateEffectDecodeResult decode_compact_private_effects(
        const std::vector<std::uint8_t>& body,
        const CompactPrivateEffectLimits& limits) {
    using Error = CompactPrivateEffectDecodeError;
    if (body.size() > limits.max_bytes)
        return {Error::body_too_large, std::nullopt};
    private_detail::Reader reader(body);
    std::array<std::uint8_t, 4> magic{};
    if (!reader.read_array(magic)) return {Error::truncated, std::nullopt};
    if (magic != compact_private_detail::magic)
        return {Error::invalid_magic, std::nullopt};

    CompactPrivateEffects effects;
    if (!reader.read_little(effects.version))
        return {Error::truncated, std::nullopt};
    if (effects.version != compact_private_effect_version)
        return {Error::unsupported_version, std::nullopt};
    if (!reader.read_little(effects.flags))
        return {Error::truncated, std::nullopt};
    if (effects.flags != compact_private_effect_flags)
        return {Error::invalid_flags, std::nullopt};
    std::uint64_t value_balance = 0U;
    std::uint64_t fee = 0U;
    if (!reader.read_little(value_balance) || !reader.read_little(fee))
        return {Error::truncated, std::nullopt};
    effects.value_balance = compact_private_detail::decode_amount(value_balance);
    if (fee > static_cast<std::uint64_t>(std::numeric_limits<Amount>::max()))
        return {Error::invalid_fee, std::nullopt};
    effects.fee = static_cast<Amount>(fee);

    std::uint32_t count = 0U;
    if (!reader.read_little(count)) return {Error::truncated, std::nullopt};
    if (count == 0U) return {Error::zero_actions, std::nullopt};
    if (count > limits.max_actions)
        return {Error::too_many_actions, std::nullopt};
    if (count > reader.remaining() / compact_private_action_effect_size)
        return {Error::truncated, std::nullopt};
    effects.actions.reserve(count);
    for (std::uint32_t i = 0U; i < count; ++i) {
        CompactPrivateActionEffect action;
        if (!reader.read_hash(action.value_commitment) ||
            !reader.read_hash(action.nullifier) ||
            !reader.read_hash(action.randomized_key) ||
            !reader.read_hash(action.note_commitment) ||
            !reader.read_hash(action.ephemeral_key) ||
            !reader.read_array(action.encrypted_note) ||
            !reader.read_array(action.outgoing_ciphertext))
            return {Error::truncated, std::nullopt};
        effects.actions.push_back(std::move(action));
    }
    if (!reader.exhausted()) return {Error::trailing_bytes, std::nullopt};
    return {Error::none,
            std::optional<CompactPrivateEffects>{std::move(effects)}};
}

inline Hash256 compact_private_effect_digest(
        const CompactPrivateEffects& effects) {
    const auto encoded = encode_compact_private_effects(effects);
    constexpr std::array<std::uint8_t, 22> domain{
        'O', 'n', 'u', 'r', 'o', 's', 'C', 'o', 'm', 'p', 'a', 'c', 't',
        'E', 'f', 'f', 'e', 'c', 't', 'V', '1'};
    std::vector<std::uint8_t> preimage(domain.begin(), domain.end());
    preimage.insert(preimage.end(), encoded.begin(), encoded.end());
    return double_sha256(preimage);
}

} // namespace onuros
