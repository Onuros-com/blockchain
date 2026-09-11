#pragma once

#include "onuros/block_format.hpp"

#include <cstddef>
#include <cstdint>

namespace onuros {

enum class BlockValidationError {
    none,
    unsupported_block_version,
    unsupported_transaction_version,
    too_many_transactions,
    transaction_too_large,
    block_too_large,
    invalid_transaction_root,
    invalid_height,
    invalid_parent,
    timestamp_not_after_median,
    timestamp_too_far_in_future,
    unexpected_target,
    invalid_proof_of_work
};

struct BlockValidationLimits {
    std::uint32_t block_version;
    std::uint32_t transaction_version;
    std::size_t max_block_bytes;
    std::uint32_t max_transactions;
    std::uint32_t max_transaction_body_bytes;
};

struct BlockContext {
    Height expected_height;
    Hash256 expected_parent;
    std::uint64_t median_time_past;
    std::uint64_t adjusted_time;
    std::uint64_t max_future_seconds;
    std::uint32_t expected_compact_target;
};

template <typename PowVerifier>
BlockValidationError validate_block(const Block& block,
                                    const BlockValidationLimits& limits,
                                    const BlockContext& context,
                                    PowVerifier verify_pow) {
    if (block.header.version != limits.block_version)
        return BlockValidationError::unsupported_block_version;
    if (block.transactions.size() > limits.max_transactions)
        return BlockValidationError::too_many_transactions;
    std::size_t encoded_size = block_prefix_encoded_size;
    if (encoded_size > limits.max_block_bytes)
        return BlockValidationError::block_too_large;
    for (const auto& transaction : block.transactions) {
        if (transaction.version != limits.transaction_version)
            return BlockValidationError::unsupported_transaction_version;
        if (transaction.body.size() > limits.max_transaction_body_bytes)
            return BlockValidationError::transaction_too_large;
        if (transaction.body.size() > limits.max_block_bytes - encoded_size ||
            8U > limits.max_block_bytes - encoded_size - transaction.body.size())
            return BlockValidationError::block_too_large;
        encoded_size += 8U + transaction.body.size();
    }
    if (!has_valid_transaction_root(block))
        return BlockValidationError::invalid_transaction_root;
    if (block.header.height != context.expected_height)
        return BlockValidationError::invalid_height;
    if (block.header.previous != context.expected_parent)
        return BlockValidationError::invalid_parent;
    if (block.header.timestamp <= context.median_time_past)
        return BlockValidationError::timestamp_not_after_median;
    if (block.header.timestamp > context.adjusted_time &&
        block.header.timestamp - context.adjusted_time > context.max_future_seconds)
        return BlockValidationError::timestamp_too_far_in_future;
    if (block.header.compact_target != context.expected_compact_target)
        return BlockValidationError::unexpected_target;
    if (!verify_pow(block.header))
        return BlockValidationError::invalid_proof_of_work;
    return BlockValidationError::none;
}

} // namespace onuros
