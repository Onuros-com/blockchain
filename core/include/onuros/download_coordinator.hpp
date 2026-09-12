#pragma once

#include "onuros/block_transfer.hpp"
#include "onuros/block_store.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace onuros {

using PeerIdentifier = std::uint64_t;

enum class DownloadClaimResult {
    claimed,
    already_owned_by_peer,
    owned_by_other_peer,
    capacity_reached
};

struct DownloadLease {
    PeerIdentifier peer = 0U;
    std::uint64_t expires_at = 0U;
};

struct BandwidthAccounting {
    std::uint64_t useful_bytes = 0U;
    std::uint64_t duplicate_bytes = 0U;
    std::uint64_t avoided_duplicate_bytes = 0U;
    std::uint64_t resumed_bytes = 0U;

    std::uint64_t wire_bytes() const noexcept {
        return useful_bytes + duplicate_bytes;
    }
};

class BlockDownloadCoordinator {
    std::size_t maximum_downloads_;
    std::uint64_t lease_seconds_;
    std::map<Hash256, DownloadLease> leases_;
    std::set<Hash256> completed_;
    std::set<Hash256> transaction_requests_;
    BandwidthAccounting accounting_;

public:
    BlockDownloadCoordinator(std::size_t maximum_downloads,
                             std::uint64_t lease_seconds)
        : maximum_downloads_(maximum_downloads), lease_seconds_(lease_seconds) {}

    DownloadClaimResult claim(const Hash256& block, PeerIdentifier peer,
                              std::uint64_t now) {
        if (completed_.find(block) != completed_.end())
            return DownloadClaimResult::owned_by_other_peer;
        const auto found = leases_.find(block);
        if (found != leases_.end()) {
            if (now < found->second.expires_at)
                return found->second.peer == peer
                    ? DownloadClaimResult::already_owned_by_peer
                    : DownloadClaimResult::owned_by_other_peer;
            leases_.erase(found);
        }
        if (leases_.size() >= maximum_downloads_)
            return DownloadClaimResult::capacity_reached;
        const auto expires = lease_seconds_ > UINT64_MAX - now
            ? UINT64_MAX : now + lease_seconds_;
        leases_[block] = {peer, expires};
        return DownloadClaimResult::claimed;
    }

    bool accepts_from(const Hash256& block, PeerIdentifier peer,
                      std::uint64_t now) const {
        const auto found = leases_.find(block);
        return found != leases_.end() && found->second.peer == peer &&
               now < found->second.expires_at;
    }

    void release_peer(PeerIdentifier peer) {
        for (auto item = leases_.begin(); item != leases_.end();) {
            if (item->second.peer == peer) item = leases_.erase(item);
            else ++item;
        }
    }

    void complete(const Hash256& block) {
        leases_.erase(block);
        completed_.insert(block);
    }

    std::vector<Hash256> reserve_transactions(
            const std::vector<Hash256>& identifiers) {
        std::vector<Hash256> result;
        result.reserve(identifiers.size());
        for (const auto& identifier : identifiers) {
            if (transaction_requests_.insert(identifier).second)
                result.push_back(identifier);
        }
        return result;
    }

    void finish_transaction(const Hash256& identifier) {
        transaction_requests_.erase(identifier);
    }

    void record_useful(std::size_t bytes) { accounting_.useful_bytes += bytes; }
    void record_duplicate(std::size_t bytes) { accounting_.duplicate_bytes += bytes; }
    void record_avoided(std::size_t bytes) {
        accounting_.avoided_duplicate_bytes += bytes;
    }
    void record_resumed(std::size_t bytes) { accounting_.resumed_bytes += bytes; }

    const BandwidthAccounting& accounting() const noexcept { return accounting_; }
    std::size_t active_downloads() const noexcept { return leases_.size(); }
};

class ResumableBlockTransactionAssembler {
    Hash256 block_identifier_{};
    BlockChunkLimits limits_;
    bool initialized_ = false;
    std::uint32_t expected_chunks_ = 0U;
    std::uint32_t expected_transactions_ = 0U;
    std::uint32_t expected_bytes_ = 0U;
    std::size_t received_bytes_ = 0U;
    std::map<std::uint32_t, BlockTransactionChunk> chunks_;
    std::map<std::uint32_t, TransactionEnvelope> transactions_;

public:
    ResumableBlockTransactionAssembler(Hash256 block_identifier,
                                       BlockChunkLimits limits)
        : block_identifier_(block_identifier), limits_(limits) {}

    BlockChunkAssemblyError add(const BlockTransactionChunk& chunk) {
        if (chunk.block_identifier != block_identifier_)
            return BlockChunkAssemblyError::wrong_block;
        if (!initialized_) {
            if (chunk.total_chunks == 0U ||
                chunk.total_chunks > limits_.maximum_total_chunks ||
                chunk.total_transactions > limits_.maximum_total_transactions ||
                chunk.total_transfer_bytes > limits_.maximum_total_transfer_bytes)
                return BlockChunkAssemblyError::count_limit;
            initialized_ = true;
            expected_chunks_ = chunk.total_chunks;
            expected_transactions_ = chunk.total_transactions;
            expected_bytes_ = chunk.total_transfer_bytes;
        } else if (chunk.total_chunks != expected_chunks_ ||
                   chunk.total_transactions != expected_transactions_ ||
                   chunk.total_transfer_bytes != expected_bytes_) {
            return BlockChunkAssemblyError::inconsistent_manifest;
        }
        if (chunk.sequence >= expected_chunks_)
            return BlockChunkAssemblyError::out_of_order;
        const auto duplicate = chunks_.find(chunk.sequence);
        if (duplicate != chunks_.end()) {
            return encode_block_transaction_chunk(duplicate->second) ==
                   encode_block_transaction_chunk(chunk)
                ? BlockChunkAssemblyError::none
                : BlockChunkAssemblyError::inconsistent_manifest;
        }
        if (chunk.transactions.empty() ||
            chunk.transactions.size() > limits_.maximum_transactions_per_chunk ||
            transactions_.size() > expected_transactions_ ||
            chunk.transactions.size() > expected_transactions_ - transactions_.size())
            return BlockChunkAssemblyError::count_limit;
        std::size_t chunk_bytes = 0U;
        std::optional<std::uint32_t> previous;
        for (const auto& item : chunk.transactions) {
            if (item.transaction.body.size() > limits_.maximum_transaction_body_bytes)
                return BlockChunkAssemblyError::byte_limit;
            if ((previous && item.index <= *previous) ||
                transactions_.find(item.index) != transactions_.end())
                return BlockChunkAssemblyError::duplicate_index;
            const auto bytes = indexed_transaction_encoded_size(item);
            if (bytes > limits_.maximum_total_transfer_bytes -
                    std::min<std::size_t>(limits_.maximum_total_transfer_bytes,
                                          received_bytes_ + chunk_bytes))
                return BlockChunkAssemblyError::byte_limit;
            chunk_bytes += bytes;
            previous = item.index;
        }
        if (received_bytes_ > expected_bytes_ ||
            chunk_bytes > expected_bytes_ - received_bytes_)
            return BlockChunkAssemblyError::inconsistent_manifest;
        for (const auto& item : chunk.transactions)
            transactions_.emplace(item.index, item.transaction);
        received_bytes_ += chunk_bytes;
        chunks_.emplace(chunk.sequence, chunk);
        return BlockChunkAssemblyError::none;
    }

    std::vector<std::uint32_t> missing_sequences() const {
        std::vector<std::uint32_t> result;
        for (std::uint32_t sequence = 0U; sequence < expected_chunks_; ++sequence) {
            if (chunks_.find(sequence) == chunks_.end()) result.push_back(sequence);
        }
        return result;
    }

    bool complete() const noexcept {
        return initialized_ && chunks_.size() == expected_chunks_ &&
               transactions_.size() == expected_transactions_ &&
               received_bytes_ == expected_bytes_;
    }
    std::size_t received_bytes() const noexcept { return received_bytes_; }
    const std::map<std::uint32_t, TransactionEnvelope>& transactions() const {
        return transactions_;
    }
    const std::map<std::uint32_t, BlockTransactionChunk>& chunks() const {
        return chunks_;
    }
};

inline std::vector<std::uint8_t> encode_resume_checkpoint(
        const ResumableBlockTransactionAssembler& assembler) {
    std::vector<std::uint8_t> output;
    detail::append_little_endian(output,
        static_cast<std::uint32_t>(assembler.chunks().size()));
    for (const auto& entry : assembler.chunks()) {
        const auto encoded = encode_block_transaction_chunk(entry.second);
        if (encoded.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("checkpoint chunk is too large");
        detail::append_little_endian(output,
            static_cast<std::uint32_t>(encoded.size()));
        output.insert(output.end(), encoded.begin(), encoded.end());
    }
    return output;
}

inline bool restore_resume_checkpoint(
        const std::vector<std::uint8_t>& input,
        ResumableBlockTransactionAssembler& assembler,
        const BlockChunkLimits& limits) {
    detail::ByteReader reader(input);
    auto candidate = assembler;
    std::uint32_t count = 0U;
    if (!reader.read_little_endian(count) || count > limits.maximum_total_chunks)
        return false;
    for (std::uint32_t i = 0U; i < count; ++i) {
        std::uint32_t size = 0U;
        std::vector<std::uint8_t> bytes;
        if (!reader.read_little_endian(size) || size > limits.maximum_chunk_bytes ||
            !reader.read_bytes(size, bytes))
            return false;
        const auto chunk = decode_block_transaction_chunk(bytes, limits);
        if (!chunk || candidate.add(*chunk) != BlockChunkAssemblyError::none)
            return false;
    }
    if (!reader.exhausted()) return false;
    assembler = std::move(candidate);
    return true;
}

inline bool save_resume_checkpoint(
        const std::filesystem::path& path,
        const ResumableBlockTransactionAssembler& assembler) {
    return detail::create_atomic(path, encode_resume_checkpoint(assembler));
}

inline bool load_resume_checkpoint(
        const std::filesystem::path& path,
        ResumableBlockTransactionAssembler& assembler,
        const BlockChunkLimits& limits) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    const auto per_chunk = static_cast<std::uintmax_t>(
        limits.maximum_chunk_bytes) + sizeof(std::uint32_t);
    if (error || (limits.maximum_total_chunks != 0U &&
            per_chunk > (std::numeric_limits<std::uintmax_t>::max() - 4U) /
                limits.maximum_total_chunks))
        return false;
    const auto maximum = 4U + per_chunk * limits.maximum_total_chunks;
    if (size > maximum || size > std::numeric_limits<std::size_t>::max())
        return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input || input.peek() != std::ifstream::traits_type::eof()) return false;
    return restore_resume_checkpoint(bytes, assembler, limits);
}

} // namespace onuros
