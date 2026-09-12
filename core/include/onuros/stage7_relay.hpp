#pragma once

#include "onuros/block_transfer.hpp"
#include "onuros/p2p_protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace onuros {

class ValidatedRelayPool {
    std::size_t maximum_transactions_;
    std::size_t maximum_bytes_;
    std::size_t bytes_ = 0U;
    std::map<Hash256, TransactionEnvelope> transactions_;

public:
    ValidatedRelayPool(std::size_t maximum_transactions,
                       std::size_t maximum_bytes)
        : maximum_transactions_(maximum_transactions),
          maximum_bytes_(maximum_bytes) {}

    // The caller must perform ordinary transaction/proof admission first.
    // This cache is transport state and never authorizes consensus acceptance.
    bool remember_validated(const TransactionEnvelope& transaction) {
        const auto identifier = transaction_id(transaction);
        if (transactions_.find(identifier) != transactions_.end()) return false;
        const auto encoded_bytes = encode_transaction(transaction).size();
        if (transactions_.size() >= maximum_transactions_ ||
            encoded_bytes > maximum_bytes_ - std::min(maximum_bytes_, bytes_))
            return false;
        transactions_.emplace(identifier, transaction);
        bytes_ += encoded_bytes;
        return true;
    }

    bool forget_validated(const Hash256& identifier) {
        const auto found = transactions_.find(identifier);
        if (found == transactions_.end()) return false;
        bytes_ -= encode_transaction(found->second).size();
        transactions_.erase(found);
        return true;
    }

    std::vector<Hash256> missing(const std::vector<Hash256>& inventory,
                                 std::size_t maximum_result) const {
        std::vector<Hash256> result;
        result.reserve(std::min(inventory.size(), maximum_result));
        for (const auto& identifier : inventory) {
            if (transactions_.find(identifier) == transactions_.end()) {
                if (result.size() == maximum_result) break;
                result.push_back(identifier);
            }
        }
        return result;
    }

    const std::map<Hash256, TransactionEnvelope>& transactions() const {
        return transactions_;
    }

    std::size_t size() const noexcept { return transactions_.size(); }
    std::size_t bytes() const noexcept { return bytes_; }
};

enum class CompactDownloadError {
    none,
    already_started,
    chunk_rejected,
    unsolicited_transaction,
    transaction_id_mismatch,
    transaction_admission_failed,
    reconstruction_failed
};

struct CompactDownloadResult {
    CompactDownloadError error = CompactDownloadError::none;
    std::optional<Block> block;
};

class CompactBlockDownload {
    CompactBlockAnnouncement announcement_;
    ValidatedRelayPool& pool_;
    BlockChunkLimits limits_;
    std::function<bool(const TransactionEnvelope&)> transaction_admission_;
    std::optional<BlockTransactionAssembler> assembler_;
    std::set<std::uint32_t> expected_missing_;
    bool started_ = false;

public:
    CompactBlockDownload(CompactBlockAnnouncement announcement,
                         ValidatedRelayPool& pool,
                         BlockChunkLimits limits,
                         std::function<bool(const TransactionEnvelope&)>
                             transaction_admission)
        : announcement_(std::move(announcement)), pool_(pool), limits_(limits),
          transaction_admission_(std::move(transaction_admission)) {}

    std::optional<MissingTransactionRequest> start(
            CompactDownloadResult& immediate_result) {
        if (started_) {
            immediate_result.error = CompactDownloadError::already_started;
            return std::nullopt;
        }
        started_ = true;
        const auto reconstruction = reconstruct_compact_block(
            announcement_, pool_.transactions());
        if (reconstruction.complete()) {
            immediate_result.block = reconstruction.block;
            return std::nullopt;
        }
        if (reconstruction.error !=
            CompactBlockReconstructionError::missing_transactions) {
            immediate_result.error = CompactDownloadError::reconstruction_failed;
            return std::nullopt;
        }
        expected_missing_.insert(reconstruction.missing_indexes.begin(),
                                 reconstruction.missing_indexes.end());
        assembler_.emplace(block_id(announcement_.header), limits_);
        return MissingTransactionRequest{block_id(announcement_.header),
                                         reconstruction.missing_indexes};
    }

    CompactDownloadResult add_chunk(const BlockTransactionChunk& chunk) {
        if (!started_ || !assembler_)
            return {CompactDownloadError::chunk_rejected, std::nullopt};
        const auto assembled = assembler_->add(chunk);
        if (assembled != BlockChunkAssemblyError::none)
            return {CompactDownloadError::chunk_rejected, std::nullopt};
        if (!assembler_->complete()) return {};
        for (const auto& entry : assembler_->transactions()) {
            if (expected_missing_.find(entry.first) == expected_missing_.end() ||
                entry.first >= announcement_.transaction_ids.size())
                return {CompactDownloadError::unsolicited_transaction, std::nullopt};
            const auto& expected = announcement_.transaction_ids[entry.first];
            if (transaction_id(entry.second) != expected)
                return {CompactDownloadError::transaction_id_mismatch, std::nullopt};
            if (!transaction_admission_ || !transaction_admission_(entry.second))
                return {CompactDownloadError::transaction_admission_failed,
                        std::nullopt};
        }
        for (const auto& entry : assembler_->transactions())
            (void)pool_.remember_validated(entry.second);
        const auto reconstruction = reconstruct_compact_block(
            announcement_, pool_.transactions());
        if (!reconstruction.complete())
            return {CompactDownloadError::reconstruction_failed, std::nullopt};
        return {CompactDownloadError::none, reconstruction.block};
    }
};

struct RelayBandwidth {
    std::size_t full_block_bytes = 0U;
    std::size_t announcement_bytes = 0U;
    std::size_t request_bytes = 0U;
    std::size_t response_bytes = 0U;

    std::size_t compact_phase_bytes() const noexcept {
        return announcement_bytes + request_bytes + response_bytes;
    }
};

inline std::optional<RelayBandwidth> measure_compact_relay(
        const Block& block, std::size_t receiver_transaction_count,
        std::size_t maximum_chunk_bytes) {
    if (receiver_transaction_count > block.transactions.size())
        return std::nullopt;
    RelayBandwidth result;
    result.full_block_bytes = encode_block(block).size();
    const auto announcement = make_compact_block_announcement(block);
    result.announcement_bytes =
        encode_compact_block_announcement(announcement).size();
    std::vector<std::uint32_t> missing;
    for (std::size_t i = receiver_transaction_count;
         i < block.transactions.size(); ++i)
        missing.push_back(static_cast<std::uint32_t>(i));
    if (missing.empty()) return result;
    const MissingTransactionRequest request{block_id(block.header), missing};
    result.request_bytes = encode_missing_transaction_request(request).size();
    const auto chunks = make_block_transaction_chunks(
        block, missing, maximum_chunk_bytes);
    if (!chunks) return std::nullopt;
    for (const auto& chunk : *chunks)
        result.response_bytes += encode_block_transaction_chunk(chunk).size();
    return result;
}

} // namespace onuros
