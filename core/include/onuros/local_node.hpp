#pragma once

#include "onuros/active_chain.hpp"
#include "onuros/block_store.hpp"
#include "onuros/block_validation.hpp"
#include "onuros/difficulty.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace onuros {

// A proof engine returns no hash when proof-specific commitments are invalid.
// This keeps invalid GPU output distinct from every possible 256-bit work hash.
using PowHashFunction = std::function<std::optional<Hash256>(const BlockHeader&)>;

struct LocalNodeParameters {
    BlockValidationLimits validation_limits;
    DecodeLimits decode_limits;
    DifficultyParameters difficulty;
    std::uint64_t max_future_seconds = 120U;
    std::size_t max_database_bytes = 64U * 1024U * 1024U;
};

enum class LocalNodeError {
    none,
    not_open,
    database_error,
    missing_parent,
    invalid_consensus_context,
    block_validation_failed,
    invalid_work,
    chain_index_rejected,
    reorganization_rejected,
    nonce_space_exhausted
};

struct LocalNodeResult {
    LocalNodeError error = LocalNodeError::none;
    BlockValidationError validation_error = BlockValidationError::none;
    BlockStoreError store_error = BlockStoreError::none;
};

class LocalNode {
    LocalNodeParameters parameters_;
    PowHashFunction pow_hash_;
    PersistentBlockStore store_;
    ActiveChainState active_state_;
    bool open_ = false;

    std::optional<std::uint32_t> expected_target(const StoredBlock& parent,
                                                  Height next_height) const {
        const auto interval = parameters_.difficulty.retarget_interval;
        if (interval < 2U) return std::nullopt;
        std::uint64_t first_timestamp = parent.block.header.timestamp;
        if (next_height % interval == 0U) {
            auto cursor = &parent;
            for (std::uint32_t i = 1U; i < interval; ++i) {
                cursor = store_.find(cursor->block.header.previous);
                if (cursor == nullptr) return std::nullopt;
            }
            first_timestamp = cursor->block.header.timestamp;
        }
        return next_compact_target(parent.block.header.compact_target, next_height,
            first_timestamp, parent.block.header.timestamp, parameters_.difficulty);
    }

    std::optional<std::uint64_t> parent_median_time(const StoredBlock& parent) const {
        std::vector<std::uint64_t> timestamps;
        timestamps.reserve(11U);
        auto cursor = &parent;
        for (std::size_t i = 0; i < 11U && cursor != nullptr; ++i) {
            timestamps.push_back(cursor->block.header.timestamp);
            if (cursor->block.header.height == 0U) break;
            cursor = store_.find(cursor->block.header.previous);
        }
        return median_time_past(timestamps);
    }

    bool audit_recovered_blocks() const {
        for (const auto& stored : store_.blocks()) {
            const auto& block = stored.block;
            BlockContext context{};
            if (block.header.height == 0U) {
                context.expected_height = 0U;
                context.expected_parent = {};
                context.median_time_past = 0U;
                context.expected_compact_target =
                    encode_compact_target(parameters_.difficulty.proof_of_work_limit);
            } else {
                const auto* parent = store_.find(block.header.previous);
                if (parent == nullptr ||
                    parent->block.header.height == std::numeric_limits<Height>::max())
                    return false;
                context.expected_height = parent->block.header.height + 1U;
                context.expected_parent = block.header.previous;
                const auto median = parent_median_time(*parent);
                const auto target = expected_target(*parent, context.expected_height);
                if (!median || !target) return false;
                context.median_time_past = *median;
                context.expected_compact_target = *target;
            }
            context.adjusted_time = std::numeric_limits<std::uint64_t>::max();
            context.max_future_seconds = parameters_.max_future_seconds;
            const auto verify_pow = [this](const BlockHeader& header) {
                const auto hash = pow_hash_(header);
                return hash && hash_meets_compact_target(*hash,
                    header.compact_target,
                    parameters_.difficulty.proof_of_work_limit);
            };
            const auto validation = stored.body_retained
                ? validate_block(block, parameters_.validation_limits,
                                 context, verify_pow)
                : validate_block_header(block.header,
                                        parameters_.validation_limits,
                                        context, verify_pow);
            if (validation != BlockValidationError::none)
                return false;
            const auto target_work = work_for_compact_target(
                block.header.compact_target,
                parameters_.difficulty.proof_of_work_limit);
            if (!target_work ||
                !(stored.work == chain_work_from_target_work(*target_work)))
                return false;
        }
        return true;
    }

public:
    LocalNode(LocalNodeParameters parameters, PowHashFunction pow_hash)
        : parameters_(std::move(parameters)), pow_hash_(std::move(pow_hash)),
          store_(parameters_.decode_limits, parameters_.max_database_bytes) {}

    LocalNodeResult open(const std::filesystem::path& path) {
        const auto error = store_.open(path);
        if (error != BlockStoreError::none)
            return {LocalNodeError::database_error, BlockValidationError::none, error};
        if (!audit_recovered_blocks())
            return {LocalNodeError::database_error, BlockValidationError::none,
                    BlockStoreError::invalid_chain};
        const auto rebuilt = rebuild_active_chain(store_.index());
        if (!rebuilt)
            return {LocalNodeError::database_error, BlockValidationError::none,
                    BlockStoreError::invalid_chain};
        active_state_ = *rebuilt;
        open_ = true;
        return {};
    }

    std::optional<Block> make_candidate(
            std::vector<TransactionEnvelope> transactions,
            std::uint64_t timestamp) const {
        if (!open_) return std::nullopt;
        Block block;
        block.header.version = parameters_.validation_limits.block_version;
        block.header.timestamp = timestamp;
        block.transactions = std::move(transactions);
        block.header.transactions_root = transaction_root(block.transactions);
        const auto* tip = store_.index().active_tip();
        if (tip == nullptr) {
            block.header.height = 0U;
            block.header.compact_target =
                encode_compact_target(parameters_.difficulty.proof_of_work_limit);
            return block;
        }
        if (tip->height == std::numeric_limits<Height>::max()) return std::nullopt;
        const auto* parent = store_.find(tip->id);
        if (parent == nullptr) return std::nullopt;
        block.header.height = tip->height + 1U;
        block.header.previous = tip->id;
        const auto target = expected_target(*parent, block.header.height);
        if (!target) return std::nullopt;
        block.header.compact_target = *target;
        return block;
    }

    LocalNodeResult submit(const Block& block, std::uint64_t adjusted_time) {
        if (!open_) return {LocalNodeError::not_open};
        BlockContext context{};
        if (store_.blocks().empty()) {
            context.expected_height = 0U;
            context.expected_parent = {};
            context.median_time_past = 0U;
            context.expected_compact_target =
                encode_compact_target(parameters_.difficulty.proof_of_work_limit);
        } else {
            const auto* parent = store_.find(block.header.previous);
            if (parent == nullptr) return {LocalNodeError::missing_parent};
            if (parent->block.header.height == std::numeric_limits<Height>::max())
                return {LocalNodeError::invalid_consensus_context};
            context.expected_height = parent->block.header.height + 1U;
            context.expected_parent = block.header.previous;
            const auto median = parent_median_time(*parent);
            const auto target = expected_target(*parent, context.expected_height);
            if (!median || !target)
                return {LocalNodeError::invalid_consensus_context};
            context.median_time_past = *median;
            context.expected_compact_target = *target;
        }
        context.adjusted_time = adjusted_time;
        context.max_future_seconds = parameters_.max_future_seconds;
        const auto validation = validate_block(block, parameters_.validation_limits,
            context, [this](const BlockHeader& header) {
                const auto hash = pow_hash_(header);
                return hash && hash_meets_compact_target(*hash,
                    header.compact_target, parameters_.difficulty.proof_of_work_limit);
            });
        if (validation != BlockValidationError::none)
            return {LocalNodeError::block_validation_failed, validation};

        const auto target_work = work_for_compact_target(block.header.compact_target,
            parameters_.difficulty.proof_of_work_limit);
        if (!target_work || is_zero(*target_work)) return {LocalNodeError::invalid_work};
        const auto work = chain_work_from_target_work(*target_work);

        ChainIndexResult committed_result;
        const auto store_error = store_.append(block, work, &committed_result);
        if (store_error != BlockStoreError::none)
            return {LocalNodeError::database_error, BlockValidationError::none,
                    store_error};
        if (block.header.height == 0U) {
            const auto rebuilt = rebuild_active_chain(store_.index());
            if (!rebuilt) return {LocalNodeError::reorganization_rejected};
            active_state_ = *rebuilt;
        } else if (committed_result.reorganization &&
                   apply_reorganization_atomically(active_state_,
                       *committed_result.reorganization, store_.index()) !=
                       ReorganizationError::none) {
            const auto rebuilt = rebuild_active_chain(store_.index());
            if (!rebuilt) return {LocalNodeError::reorganization_rejected};
            active_state_ = *rebuilt;
        }
        return {};
    }

    LocalNodeResult mine(Block& candidate, std::uint64_t adjusted_time,
                         std::uint64_t max_attempts) {
        for (std::uint64_t attempt = 0U; attempt < max_attempts; ++attempt) {
            const auto hash = pow_hash_(candidate.header);
            if (hash && hash_meets_compact_target(*hash,
                    candidate.header.compact_target,
                    parameters_.difficulty.proof_of_work_limit))
                return submit(candidate, adjusted_time);
            if (candidate.header.nonce == std::numeric_limits<std::uint64_t>::max())
                return {LocalNodeError::nonce_space_exhausted};
            ++candidate.header.nonce;
        }
        return {LocalNodeError::nonce_space_exhausted};
    }

    LocalNodeResult compact_history(
            const PruningPolicy& policy,
            const PruningCheckpoint& checkpoint) {
        if (!open_) return {LocalNodeError::not_open};
        const auto error = store_.compact(policy, checkpoint);
        if (error != BlockStoreError::none)
            return {LocalNodeError::database_error,
                    BlockValidationError::none, error};
        return {};
    }

    const PersistentBlockStore& store() const { return store_; }
    const ActiveChainState& active_state() const { return active_state_; }
};

} // namespace onuros
