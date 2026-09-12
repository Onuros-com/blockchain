#pragma once

#include "onuros/block_format.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace onuros {

// A compact announcement never replaces consensus validation. It only avoids
// sending transaction bodies that a peer already received through mempool
// relay. Full 256-bit transaction identifiers are used initially so the first
// protocol does not introduce short-ID collision handling into consensus-adjacent
// code.
struct CompactBlockAnnouncement {
    BlockHeader header;
    std::vector<Hash256> transaction_ids;
};

inline constexpr std::size_t compact_block_prefix_encoded_size =
    block_header_encoded_size + sizeof(std::uint32_t);

enum class CompactBlockReconstructionError {
    none,
    missing_transactions,
    transaction_id_mismatch,
    transaction_root_mismatch
};

struct CompactBlockReconstructionResult {
    CompactBlockReconstructionError error =
        CompactBlockReconstructionError::none;
    Block block;
    std::vector<std::uint32_t> missing_indexes;

    bool complete() const noexcept {
        return error == CompactBlockReconstructionError::none;
    }
};

inline CompactBlockAnnouncement make_compact_block_announcement(
        const Block& block) {
    CompactBlockAnnouncement announcement;
    announcement.header = block.header;
    announcement.transaction_ids.reserve(block.transactions.size());
    for (const auto& transaction : block.transactions)
        announcement.transaction_ids.push_back(transaction_id(transaction));
    return announcement;
}

inline std::vector<std::uint8_t> encode_compact_block_announcement(
        const CompactBlockAnnouncement& announcement) {
    if (announcement.transaction_ids.size() >
        std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("compact block transaction count exceeds field");
    auto output = encode_block_header(announcement.header);
    detail::append_little_endian(output,
        static_cast<std::uint32_t>(announcement.transaction_ids.size()));
    for (const auto& id : announcement.transaction_ids)
        detail::append_hash(output, id);
    return output;
}

inline std::optional<CompactBlockAnnouncement>
decode_compact_block_announcement(const std::vector<std::uint8_t>& input,
                                  std::uint32_t max_transactions,
                                  std::size_t max_announcement_bytes) {
    if (input.size() < compact_block_prefix_encoded_size ||
        input.size() > max_announcement_bytes)
        return std::nullopt;
    detail::ByteReader reader(input);
    CompactBlockAnnouncement announcement;
    std::uint32_t count = 0U;
    if (!detail::read_block_header(reader, announcement.header) ||
        !reader.read_little_endian(count) || count > max_transactions ||
        static_cast<std::size_t>(count) > reader.remaining() / Hash256{}.size())
        return std::nullopt;
    if (reader.remaining() != static_cast<std::size_t>(count) * Hash256{}.size())
        return std::nullopt;
    announcement.transaction_ids.reserve(count);
    for (std::uint32_t i = 0U; i < count; ++i) {
        Hash256 id{};
        if (!reader.read_hash(id)) return std::nullopt;
        announcement.transaction_ids.push_back(id);
    }
    if (!reader.exhausted()) return std::nullopt;
    return announcement;
}

inline CompactBlockReconstructionResult reconstruct_compact_block(
        const CompactBlockAnnouncement& announcement,
        const std::map<Hash256, TransactionEnvelope>&
            available_transactions) {
    CompactBlockReconstructionResult result;
    result.block.header = announcement.header;
    result.block.transactions.reserve(announcement.transaction_ids.size());
    for (std::size_t i = 0U; i < announcement.transaction_ids.size(); ++i) {
        const auto& expected_id = announcement.transaction_ids[i];
        const auto found = available_transactions.find(expected_id);
        if (found == available_transactions.end()) {
            result.missing_indexes.push_back(static_cast<std::uint32_t>(i));
            continue;
        }
        if (transaction_id(found->second) != expected_id) {
            result.error = CompactBlockReconstructionError::transaction_id_mismatch;
            return result;
        }
        result.block.transactions.push_back(found->second);
    }
    if (!result.missing_indexes.empty()) {
        result.block.transactions.clear();
        result.error = CompactBlockReconstructionError::missing_transactions;
        return result;
    }
    if (!has_valid_transaction_root(result.block)) {
        result.block.transactions.clear();
        result.error = CompactBlockReconstructionError::transaction_root_mismatch;
        return result;
    }
    return result;
}

} // namespace onuros
