#pragma once

#include "onuros/consensus_limits.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>

namespace onuros {

struct BlockProductionPolicy {
    bool enabled = false;
    std::size_t sustained_bytes = 0U;
    std::size_t burst_bytes = 0U;
    std::size_t rolling_window_blocks = 0U;
};

enum class BlockProductionPolicyError {
    none,
    disabled,
    invalid_policy,
    burst_limit_exceeded,
    rolling_budget_exceeded
};

class RollingBlockProductionBudget {
    BlockProductionPolicy policy_;
    std::deque<std::size_t> history_;
    std::size_t rolling_bytes_ = 0U;

    bool valid_policy() const noexcept {
        return policy_.sustained_bytes != 0U &&
               policy_.burst_bytes >= policy_.sustained_bytes &&
               policy_.burst_bytes <= max_serialized_block_bytes &&
               policy_.rolling_window_blocks != 0U &&
               policy_.sustained_bytes <=
                   std::numeric_limits<std::size_t>::max() /
                       policy_.rolling_window_blocks;
    }

public:
    explicit RollingBlockProductionBudget(BlockProductionPolicy policy)
        : policy_(policy) {}

    BlockProductionPolicyError check(std::size_t block_bytes) const noexcept {
        if (!policy_.enabled) return BlockProductionPolicyError::disabled;
        if (!valid_policy()) return BlockProductionPolicyError::invalid_policy;
        if (block_bytes > policy_.burst_bytes)
            return BlockProductionPolicyError::burst_limit_exceeded;
        auto retained_bytes = rolling_bytes_;
        if (history_.size() == policy_.rolling_window_blocks)
            retained_bytes -= history_.front();
        if (block_bytes > std::numeric_limits<std::size_t>::max() -
                              retained_bytes)
            return BlockProductionPolicyError::rolling_budget_exceeded;
        const auto next_count = std::min(
            policy_.rolling_window_blocks, history_.size() + 1U);
        const auto allowed = policy_.sustained_bytes * next_count;
        if (retained_bytes + block_bytes > allowed)
            return BlockProductionPolicyError::rolling_budget_exceeded;
        return BlockProductionPolicyError::none;
    }

    BlockProductionPolicyError record(std::size_t block_bytes) {
        const auto result = check(block_bytes);
        if (result != BlockProductionPolicyError::none) return result;
        history_.push_back(block_bytes);
        rolling_bytes_ += block_bytes;
        if (history_.size() > policy_.rolling_window_blocks) {
            rolling_bytes_ -= history_.front();
            history_.pop_front();
        }
        return BlockProductionPolicyError::none;
    }

    std::size_t rolling_bytes() const noexcept { return rolling_bytes_; }
    std::size_t observed_blocks() const noexcept { return history_.size(); }
};

inline std::optional<std::uint64_t> projected_archival_bytes_per_year(
        std::size_t sustained_bytes, std::uint64_t block_interval_seconds) {
    if (sustained_bytes == 0U || block_interval_seconds == 0U)
        return std::nullopt;
    constexpr std::uint64_t seconds_per_year = 365U * 24U * 60U * 60U;
    const auto blocks = seconds_per_year / block_interval_seconds;
    if (blocks == 0U) return std::uint64_t{0U};
    if (sustained_bytes >
        std::numeric_limits<std::uint64_t>::max() / blocks)
        return std::nullopt;
    return static_cast<std::uint64_t>(sustained_bytes) * blocks;
}

} // namespace onuros
