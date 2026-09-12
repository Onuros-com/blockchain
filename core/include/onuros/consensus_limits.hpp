#pragma once

#include <cstddef>

namespace onuros {

// Stage 7 initial private-testnet consensus ceiling. Keep this as the single
// source of truth for block decoding, validation, persistence and P2P framing.
inline constexpr std::size_t bytes_per_mib = 1024U * 1024U;
inline constexpr std::size_t max_serialized_block_bytes = 16U * bytes_per_mib;

inline constexpr std::size_t effective_block_limit(
        std::size_t configured_limit) noexcept {
    return configured_limit < max_serialized_block_bytes
        ? configured_limit
        : max_serialized_block_bytes;
}

} // namespace onuros
