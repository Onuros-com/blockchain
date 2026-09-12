#include "onuros/kawpow.hpp"

#include <crypto/ethash/include/ethash/ethash.hpp>
#include <crypto/ethash/include/ethash/progpow.hpp>

#include <algorithm>
#include <iterator>
#include <limits>

namespace onuros {
namespace {

ethash::hash256 to_ethash(const Hash256& source) {
    return ethash::hash256_from_bytes(source.data());
}

Hash256 from_ethash(const ethash::hash256& source) {
    Hash256 result{};
    std::copy(std::begin(source.bytes), std::end(source.bytes), result.begin());
    return result;
}

} // namespace

std::vector<std::uint8_t> encode_kawpow_header_preimage(const BlockHeader& header) {
    std::vector<std::uint8_t> output;
    output.reserve(120U);
    detail::append_little_endian(output, header.version);
    detail::append_little_endian(output, header.height);
    detail::append_hash(output, header.previous);
    detail::append_hash(output, header.transactions_root);
    detail::append_hash(output, header.shielded_root);
    detail::append_little_endian(output, header.timestamp);
    detail::append_little_endian(output, header.compact_target);
    return output;
}

Hash256 kawpow_header_hash(const BlockHeader& header) {
    return double_sha256(encode_kawpow_header_preimage(header));
}

std::optional<KawpowResult> calculate_kawpow(Height height,
                                             const Hash256& header_hash,
                                             std::uint64_t nonce) {
    if (height > static_cast<Height>(std::numeric_limits<int>::max()))
        return std::nullopt;
    const auto block_number = static_cast<int>(height);
    const auto epoch = ethash::get_epoch_number(block_number);
    static thread_local int cached_epoch = -1;
    static thread_local ethash::epoch_context_ptr context{
        nullptr, ethash_destroy_epoch_context};
    if (!context || cached_epoch != epoch) {
        context = ethash::create_epoch_context(epoch);
        cached_epoch = context ? epoch : -1;
    }
    if (!context) return std::nullopt;
    const auto result = progpow::hash(*context, block_number,
                                      to_ethash(header_hash), nonce);
    return KawpowResult{from_ethash(result.final_hash),
                        from_ethash(result.mix_hash)};
}

std::optional<Hash256> kawpow_pow_hash(const BlockHeader& header) {
    const auto result = calculate_kawpow(header.height,
                                         kawpow_header_hash(header),
                                         header.nonce);
    if (!result || result->mix_hash != header.mix_hash)
        return std::nullopt;
    return result->final_hash;
}

} // namespace onuros
