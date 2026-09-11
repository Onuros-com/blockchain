#pragma once

#include "onuros/hash256.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace onuros {

// Unsigned 256-bit consensus integer stored as little-endian 32-bit limbs.
struct Target256 {
    std::array<std::uint32_t, 8> limbs{};
};

inline bool operator==(const Target256& left, const Target256& right) {
    return left.limbs == right.limbs;
}

inline bool operator<(const Target256& left, const Target256& right) {
    for (std::size_t i = left.limbs.size(); i != 0U; --i) {
        if (left.limbs[i - 1U] != right.limbs[i - 1U])
            return left.limbs[i - 1U] < right.limbs[i - 1U];
    }
    return false;
}

inline bool is_zero(const Target256& value) {
    return value == Target256{};
}

inline std::optional<Target256> decode_compact_target(std::uint32_t compact) {
    const auto size = compact >> 24U;
    const auto word = compact & 0x007fffffU;
    const bool negative = word != 0U && (compact & 0x00800000U) != 0U;
    const bool overflow = word != 0U &&
        (size > 34U || (word > 0xffU && size > 33U) ||
         (word > 0xffffU && size > 32U));
    if (word == 0U || negative || overflow) return std::nullopt;

    Target256 target;
    if (size <= 3U) {
        target.limbs[0] = word >> (8U * (3U - size));
    } else {
        const auto shift_bytes = size - 3U;
        for (std::size_t byte = 0; byte < 3U; ++byte) {
            const auto destination = static_cast<std::size_t>(shift_bytes) + byte;
            if (destination >= 32U) return std::nullopt;
            const auto value = static_cast<std::uint8_t>(word >> (8U * byte));
            target.limbs[destination / 4U] |=
                static_cast<std::uint32_t>(value) << (8U * (destination % 4U));
        }
    }
    return is_zero(target) ? std::nullopt : std::optional<Target256>{target};
}

inline std::uint32_t encode_compact_target(const Target256& target) {
    if (is_zero(target)) return 0U;
    std::size_t highest_byte = 31U;
    while (highest_byte != 0U &&
           ((target.limbs[highest_byte / 4U] >>
             (8U * (highest_byte % 4U))) & 0xffU) == 0U)
        --highest_byte;
    std::uint32_t size = static_cast<std::uint32_t>(highest_byte + 1U);
    std::uint32_t word = 0U;
    if (size <= 3U) {
        word = target.limbs[0] << (8U * (3U - size));
    } else {
        const auto start = static_cast<std::size_t>(size - 3U);
        for (std::size_t byte = 0; byte < 3U; ++byte) {
            const auto source = start + byte;
            word |= ((target.limbs[source / 4U] >> (8U * (source % 4U))) & 0xffU)
                    << (8U * byte);
        }
    }
    if ((word & 0x00800000U) != 0U) {
        word >>= 8U;
        ++size;
    }
    return (size << 24U) | (word & 0x007fffffU);
}

inline bool is_canonical_compact_target(std::uint32_t compact) {
    const auto target = decode_compact_target(compact);
    return target && encode_compact_target(*target) == compact;
}

inline bool hash_meets_target(const Hash256& hash, const Target256& target) {
    // SHA-256 output is a big-endian byte string; compare as a big-endian integer.
    for (std::size_t i = 0; i < hash.size(); ++i) {
        const auto target_byte = static_cast<std::uint8_t>(
            target.limbs[(31U - i) / 4U] >> (8U * ((31U - i) % 4U)));
        if (hash[i] != target_byte) return hash[i] < target_byte;
    }
    return true;
}

inline bool hash_meets_compact_target(const Hash256& hash, std::uint32_t compact,
                                      const Target256& proof_of_work_limit) {
    const auto target = decode_compact_target(compact);
    return target && is_canonical_compact_target(compact) &&
           !(*target < Target256{}) && !(proof_of_work_limit < *target) &&
           hash_meets_target(hash, *target);
}

inline Target256 work_for_target(const Target256& target) {
    // Exact floor(2^256 / (target + 1)), matching accumulated-work selection.
    std::array<std::uint32_t, 9> divisor{};
    std::copy(target.limbs.begin(), target.limbs.end(), divisor.begin());
    std::uint64_t carry = 1U;
    for (auto& limb : divisor) {
        const auto sum = static_cast<std::uint64_t>(limb) + carry;
        limb = static_cast<std::uint32_t>(sum);
        carry = sum >> 32U;
    }

    std::array<std::uint32_t, 9> remainder{};
    Target256 quotient;
    const auto greater_or_equal = [](const auto& left, const auto& right) {
        for (std::size_t i = left.size(); i != 0U; --i) {
            if (left[i - 1U] != right[i - 1U])
                return left[i - 1U] > right[i - 1U];
        }
        return true;
    };
    for (std::size_t bit = 257U; bit != 0U; --bit) {
        std::uint64_t shift_carry = 0U;
        for (auto& limb : remainder) {
            const auto shifted = (static_cast<std::uint64_t>(limb) << 1U) |
                                 shift_carry;
            limb = static_cast<std::uint32_t>(shifted);
            shift_carry = shifted >> 32U;
        }
        const auto input_bit = bit - 1U;
        if (input_bit == 256U) remainder[0] |= 1U;
        if (!greater_or_equal(remainder, divisor)) continue;
        std::uint64_t borrow = 0U;
        for (std::size_t i = 0; i < remainder.size(); ++i) {
            const auto subtrahend = static_cast<std::uint64_t>(divisor[i]) + borrow;
            const auto current = static_cast<std::uint64_t>(remainder[i]);
            remainder[i] = static_cast<std::uint32_t>(current - subtrahend);
            borrow = current < subtrahend ? 1U : 0U;
        }
        if (input_bit < 256U)
            quotient.limbs[input_bit / 32U] |= 1U << (input_bit % 32U);
    }
    return quotient;
}

inline std::optional<Target256> work_for_compact_target(
        std::uint32_t compact, const Target256& proof_of_work_limit) {
    const auto target = decode_compact_target(compact);
    if (!target || !is_canonical_compact_target(compact) ||
        proof_of_work_limit < *target)
        return std::nullopt;
    return work_for_target(*target);
}

struct DifficultyParameters {
    std::uint64_t target_block_seconds = 60U;
    std::uint32_t retarget_interval = 60U;
    std::uint32_t adjustment_clamp_factor = 4U;
    Target256 proof_of_work_limit{};
};

inline std::optional<Target256> multiply_divide_target(
        const Target256& target, std::uint32_t multiplier,
        std::uint32_t divisor) {
    if (multiplier == 0U || divisor == 0U) return std::nullopt;
    std::array<std::uint32_t, 9> wide{};
    std::uint64_t carry = 0U;
    for (std::size_t i = 0; i < target.limbs.size(); ++i) {
        const auto product = static_cast<std::uint64_t>(target.limbs[i]) *
                             multiplier + carry;
        wide[i] = static_cast<std::uint32_t>(product);
        carry = product >> 32U;
    }
    wide.back() = static_cast<std::uint32_t>(carry);

    std::uint64_t remainder = 0U;
    for (std::size_t i = wide.size(); i != 0U; --i) {
        const auto dividend = (remainder << 32U) | wide[i - 1U];
        wide[i - 1U] = static_cast<std::uint32_t>(dividend / divisor);
        remainder = dividend % divisor;
    }
    if (wide.back() != 0U) return std::nullopt;
    Target256 result;
    std::copy_n(wide.begin(), result.limbs.size(), result.limbs.begin());
    return result;
}

inline std::optional<std::uint32_t> next_compact_target(
        std::uint32_t previous_compact, std::uint64_t next_height,
        std::uint64_t interval_first_timestamp,
        std::uint64_t interval_last_timestamp,
        const DifficultyParameters& parameters) {
    const auto previous = decode_compact_target(previous_compact);
    if (!previous || !is_canonical_compact_target(previous_compact) ||
        is_zero(parameters.proof_of_work_limit) ||
        parameters.proof_of_work_limit < *previous ||
        parameters.target_block_seconds == 0U || parameters.retarget_interval < 2U ||
        parameters.adjustment_clamp_factor < 2U)
        return std::nullopt;
    if (next_height % parameters.retarget_interval != 0U)
        return previous_compact;
    if (interval_last_timestamp <= interval_first_timestamp)
        return std::nullopt;

    const auto blocks_between = static_cast<std::uint64_t>(parameters.retarget_interval - 1U);
    if (parameters.target_block_seconds > UINT64_MAX / blocks_between)
        return std::nullopt;
    const auto expected_span = parameters.target_block_seconds * blocks_between;
    if (expected_span > UINT32_MAX ||
        expected_span > UINT64_MAX / parameters.adjustment_clamp_factor)
        return std::nullopt;
    const auto minimum_span = expected_span / parameters.adjustment_clamp_factor;
    const auto maximum_span = expected_span * parameters.adjustment_clamp_factor;
    const auto observed_span = interval_last_timestamp - interval_first_timestamp;
    const auto bounded_span = std::max(minimum_span,
        std::min(observed_span, maximum_span));
    if (bounded_span == 0U || bounded_span > UINT32_MAX)
        return std::nullopt;

    auto adjusted = multiply_divide_target(*previous,
        static_cast<std::uint32_t>(bounded_span),
        static_cast<std::uint32_t>(expected_span));
    if (!adjusted || parameters.proof_of_work_limit < *adjusted)
        adjusted = parameters.proof_of_work_limit;
    if (is_zero(*adjusted)) adjusted->limbs[0] = 1U;
    return encode_compact_target(*adjusted);
}

inline std::optional<std::uint64_t> median_time_past(
        const std::vector<std::uint64_t>& ancestor_timestamps,
        std::size_t window = 11U) {
    if (ancestor_timestamps.empty() || window == 0U) return std::nullopt;
    const auto count = std::min(window, ancestor_timestamps.size());
    std::vector<std::uint64_t> values(ancestor_timestamps.end() -
        static_cast<std::ptrdiff_t>(count), ancestor_timestamps.end());
    std::sort(values.begin(), values.end());
    return values[values.size() / 2U];
}

} // namespace onuros
