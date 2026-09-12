#pragma once

#include "onuros/block_format.hpp"

#include <cstdint>
#include <optional>

namespace onuros {

// KawPoW revision 0.9.4, matching the Ravencoin consensus implementation
// pinned under third_party/ravencoin-kawpow. GPU miners are untrusted: nodes
// independently recompute the mix and return an impossible hash on mismatch.
struct KawpowResult {
    Hash256 final_hash{};
    Hash256 mix_hash{};
};

std::vector<std::uint8_t> encode_kawpow_header_preimage(const BlockHeader& header);
Hash256 kawpow_header_hash(const BlockHeader& header);

std::optional<KawpowResult> calculate_kawpow(Height height,
                                             const Hash256& header_hash,
                                             std::uint64_t nonce);

// Adapter for LocalNode's PowHashFunction. A bad mix hash has no work hash.
std::optional<Hash256> kawpow_pow_hash(const BlockHeader& header);

} // namespace onuros
