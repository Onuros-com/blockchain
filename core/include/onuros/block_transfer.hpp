#pragma once

#include "onuros/compact_block_relay.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace onuros {

struct MissingTransactionRequest {
    Hash256 block_identifier{};
    std::vector<std::uint32_t> indexes;
};

inline std::vector<std::uint8_t> encode_missing_transaction_request(
        const MissingTransactionRequest& request) {
    if (request.indexes.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("missing transaction request is too large");
    std::vector<std::uint8_t> output;
    output.reserve(36U + request.indexes.size() * 4U);
    detail::append_hash(output, request.block_identifier);
    detail::append_little_endian(output,
        static_cast<std::uint32_t>(request.indexes.size()));
    for (const auto index : request.indexes)
        detail::append_little_endian(output, index);
    return output;
}

inline std::optional<MissingTransactionRequest>
decode_missing_transaction_request(const std::vector<std::uint8_t>& input,
                                   std::uint32_t maximum_indexes) {
    if (input.size() < 36U) return std::nullopt;
    detail::ByteReader reader(input);
    MissingTransactionRequest request;
    std::uint32_t count = 0U;
    if (!reader.read_hash(request.block_identifier) ||
        !reader.read_little_endian(count) || count > maximum_indexes ||
        reader.remaining() != static_cast<std::size_t>(count) * 4U)
        return std::nullopt;
    request.indexes.reserve(count);
    std::uint32_t previous = 0U;
    for (std::uint32_t i = 0U; i < count; ++i) {
        std::uint32_t index = 0U;
        if (!reader.read_little_endian(index) || (i != 0U && index <= previous))
            return std::nullopt;
        request.indexes.push_back(index);
        previous = index;
    }
    return request;
}

struct IndexedTransaction {
    std::uint32_t index = 0U;
    TransactionEnvelope transaction;
};

struct BlockTransactionChunk {
    Hash256 block_identifier{};
    std::uint32_t sequence = 0U;
    std::uint32_t total_chunks = 0U;
    std::uint32_t total_transactions = 0U;
    std::uint32_t total_transfer_bytes = 0U;
    std::vector<IndexedTransaction> transactions;
};

inline constexpr std::size_t block_transaction_chunk_prefix_size = 48U;

inline std::size_t indexed_transaction_encoded_size(
        const IndexedTransaction& transaction) noexcept {
    return 12U + transaction.transaction.body.size();
}

inline std::vector<std::uint8_t> encode_block_transaction_chunk(
        const BlockTransactionChunk& chunk) {
    if (chunk.transactions.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("chunk transaction count is too large");
    std::vector<std::uint8_t> output;
    std::size_t reserve = block_transaction_chunk_prefix_size;
    for (const auto& transaction : chunk.transactions)
        reserve += indexed_transaction_encoded_size(transaction);
    output.reserve(reserve);
    detail::append_hash(output, chunk.block_identifier);
    detail::append_little_endian(output, chunk.sequence);
    detail::append_little_endian(output, chunk.total_chunks);
    detail::append_little_endian(output, chunk.total_transactions);
    detail::append_little_endian(output, chunk.total_transfer_bytes);
    detail::append_little_endian(output,
        static_cast<std::uint32_t>(chunk.transactions.size()));
    for (const auto& indexed : chunk.transactions) {
        detail::append_little_endian(output, indexed.index);
        const auto encoded = encode_transaction(indexed.transaction);
        output.insert(output.end(), encoded.begin(), encoded.end());
    }
    return output;
}

struct BlockChunkLimits {
    std::size_t maximum_chunk_bytes = 256U * 1024U;
    std::uint32_t maximum_transactions_per_chunk = 1024U;
    std::uint32_t maximum_transaction_body_bytes = 1024U * 1024U;
    std::uint32_t maximum_total_chunks = 4096U;
    std::uint32_t maximum_total_transactions = 10'000U;
    std::uint32_t maximum_total_transfer_bytes =
        static_cast<std::uint32_t>(max_serialized_block_bytes);
};

inline std::optional<BlockTransactionChunk> decode_block_transaction_chunk(
        const std::vector<std::uint8_t>& input, const BlockChunkLimits& limits) {
    if (input.size() < block_transaction_chunk_prefix_size ||
        input.size() > limits.maximum_chunk_bytes)
        return std::nullopt;
    detail::ByteReader reader(input);
    BlockTransactionChunk chunk;
    std::uint32_t item_count = 0U;
    if (!reader.read_hash(chunk.block_identifier) ||
        !reader.read_little_endian(chunk.sequence) ||
        !reader.read_little_endian(chunk.total_chunks) ||
        !reader.read_little_endian(chunk.total_transactions) ||
        !reader.read_little_endian(chunk.total_transfer_bytes) ||
        !reader.read_little_endian(item_count) ||
        chunk.total_chunks == 0U || chunk.sequence >= chunk.total_chunks ||
        chunk.total_chunks > limits.maximum_total_chunks ||
        chunk.total_transactions > limits.maximum_total_transactions ||
        chunk.total_transfer_bytes > limits.maximum_total_transfer_bytes ||
        item_count == 0U ||
        item_count > limits.maximum_transactions_per_chunk ||
        item_count > chunk.total_transactions)
        return std::nullopt;
    chunk.transactions.reserve(item_count);
    std::uint32_t previous = 0U;
    for (std::uint32_t i = 0U; i < item_count; ++i) {
        IndexedTransaction indexed;
        if (!reader.read_little_endian(indexed.index) ||
            (i != 0U && indexed.index <= previous) ||
            !detail::read_transaction(reader,
                limits.maximum_transaction_body_bytes, indexed.transaction))
            return std::nullopt;
        previous = indexed.index;
        chunk.transactions.push_back(std::move(indexed));
    }
    if (!reader.exhausted()) return std::nullopt;
    return chunk;
}

inline std::optional<std::vector<BlockTransactionChunk>>
make_block_transaction_chunks(const Block& block,
                              const std::vector<std::uint32_t>& indexes,
                              std::size_t maximum_chunk_bytes) {
    if (maximum_chunk_bytes <= block_transaction_chunk_prefix_size ||
        indexes.size() > std::numeric_limits<std::uint32_t>::max())
        return std::nullopt;
    std::vector<BlockTransactionChunk> chunks;
    BlockTransactionChunk current;
    current.block_identifier = block_id(block.header);
    std::uint32_t previous = 0U;
    std::size_t transfer_bytes = 0U;
    for (std::size_t position = 0U; position < indexes.size(); ++position) {
        const auto index = indexes[position];
        if (index >= block.transactions.size() ||
            (position != 0U && index <= previous))
            return std::nullopt;
        previous = index;
        IndexedTransaction item{index, block.transactions[index]};
        const auto item_bytes = indexed_transaction_encoded_size(item);
        if (item_bytes > maximum_chunk_bytes - block_transaction_chunk_prefix_size)
            return std::nullopt;
        std::size_t current_bytes = block_transaction_chunk_prefix_size;
        for (const auto& existing : current.transactions)
            current_bytes += indexed_transaction_encoded_size(existing);
        if (!current.transactions.empty() &&
            item_bytes > maximum_chunk_bytes - current_bytes) {
            chunks.push_back(std::move(current));
            current = {};
            current.block_identifier = block_id(block.header);
        }
        current.transactions.push_back(std::move(item));
        if (item_bytes > std::numeric_limits<std::uint32_t>::max() - transfer_bytes)
            return std::nullopt;
        transfer_bytes += item_bytes;
    }
    if (!current.transactions.empty()) chunks.push_back(std::move(current));
    if (chunks.empty() || chunks.size() > std::numeric_limits<std::uint32_t>::max() ||
        transfer_bytes > std::numeric_limits<std::uint32_t>::max())
        return std::nullopt;
    for (std::size_t i = 0U; i < chunks.size(); ++i) {
        chunks[i].sequence = static_cast<std::uint32_t>(i);
        chunks[i].total_chunks = static_cast<std::uint32_t>(chunks.size());
        chunks[i].total_transactions = static_cast<std::uint32_t>(indexes.size());
        chunks[i].total_transfer_bytes = static_cast<std::uint32_t>(transfer_bytes);
    }
    return chunks;
}

enum class BlockChunkAssemblyError {
    none,
    wrong_block,
    inconsistent_manifest,
    out_of_order,
    duplicate_index,
    byte_limit,
    count_limit
};

class BlockTransactionAssembler {
    Hash256 block_identifier_{};
    BlockChunkLimits limits_;
    bool initialized_ = false;
    std::uint32_t expected_chunks_ = 0U;
    std::uint32_t expected_transactions_ = 0U;
    std::uint32_t expected_bytes_ = 0U;
    std::uint32_t next_sequence_ = 0U;
    std::size_t received_bytes_ = 0U;
    std::map<std::uint32_t, TransactionEnvelope> transactions_;

public:
    BlockTransactionAssembler(Hash256 block_identifier, BlockChunkLimits limits)
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
        if (chunk.sequence != next_sequence_ ||
            chunk.sequence >= expected_chunks_ || next_sequence_ >= expected_chunks_)
            return BlockChunkAssemblyError::out_of_order;
        if (chunk.transactions.empty() ||
            chunk.transactions.size() > limits_.maximum_transactions_per_chunk ||
            transactions_.size() > expected_transactions_ ||
            chunk.transactions.size() >
                expected_transactions_ - transactions_.size())
            return BlockChunkAssemblyError::count_limit;
        std::size_t chunk_bytes = 0U;
        std::optional<std::uint32_t> previous_index;
        for (const auto& item : chunk.transactions) {
            if (item.transaction.body.size() >
                limits_.maximum_transaction_body_bytes)
                return BlockChunkAssemblyError::byte_limit;
            const auto item_bytes = indexed_transaction_encoded_size(item);
            if (item_bytes > limits_.maximum_total_transfer_bytes -
                    std::min<std::size_t>(limits_.maximum_total_transfer_bytes,
                                          received_bytes_ + chunk_bytes))
                return BlockChunkAssemblyError::byte_limit;
            chunk_bytes += item_bytes;
            if ((previous_index && item.index <= *previous_index) ||
                transactions_.find(item.index) != transactions_.end())
                return BlockChunkAssemblyError::duplicate_index;
            previous_index = item.index;
        }
        if (received_bytes_ > expected_bytes_ ||
            chunk_bytes > expected_bytes_ - received_bytes_)
            return BlockChunkAssemblyError::inconsistent_manifest;
        for (const auto& item : chunk.transactions)
            transactions_.emplace(item.index, item.transaction);
        received_bytes_ += chunk_bytes;
        ++next_sequence_;
        return BlockChunkAssemblyError::none;
    }

    bool complete() const noexcept {
        return initialized_ && next_sequence_ == expected_chunks_ &&
               transactions_.size() == expected_transactions_ &&
               received_bytes_ == expected_bytes_;
    }

    const std::map<std::uint32_t, TransactionEnvelope>& transactions() const {
        return transactions_;
    }
};

} // namespace onuros
