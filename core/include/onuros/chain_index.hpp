#pragma once

#include "onuros/economics.hpp"
#include "onuros/hash256.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

namespace onuros {

// Little-endian limbs provide 256-bit accumulated work without relying on a
// platform-specific integer extension. This is an in-memory type; persistence
// will define its own explicit encoding.
struct ChainWork {
    std::array<std::uint64_t, 4> limbs{};
};

inline bool operator==(const ChainWork& left, const ChainWork& right) {
    return left.limbs == right.limbs;
}

inline bool operator<(const ChainWork& left, const ChainWork& right) {
    for (std::size_t i = left.limbs.size(); i != 0; --i) {
        if (left.limbs[i - 1U] != right.limbs[i - 1U])
            return left.limbs[i - 1U] < right.limbs[i - 1U];
    }
    return false;
}

inline ChainWork chain_work(std::uint64_t value) {
    return {{{value, 0U, 0U, 0U}}};
}

inline std::optional<ChainWork> add_chain_work(ChainWork left, ChainWork right) {
    ChainWork result;
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < result.limbs.size(); ++i) {
        const auto partial = left.limbs[i] + right.limbs[i];
        const auto carry_from_partial = partial < left.limbs[i];
        const auto sum = partial + carry;
        const auto carry_from_input = sum < partial;
        result.limbs[i] = sum;
        carry = (carry_from_partial || carry_from_input) ? 1U : 0U;
    }
    if (carry != 0U) return std::nullopt;
    return result;
}

inline bool is_zero(ChainWork work) {
    return work == ChainWork{};
}

struct Hash256Hasher {
    std::size_t operator()(const Hash256& hash) const noexcept {
        std::size_t value = 1469598103934665603ULL;
        for (const auto byte : hash) {
            value ^= byte;
            value *= 1099511628211ULL;
        }
        return value;
    }
};

struct ChainEntry {
    Hash256 id;
    Hash256 parent;
    Height height;
    ChainWork block_work;
    ChainWork accumulated_work;
    std::uint64_t timestamp;
};

struct ReorganizationPlan {
    std::vector<Hash256> disconnect;
    std::vector<Hash256> connect;
};

enum class ChainIndexError {
    none,
    genesis_already_exists,
    missing_genesis,
    duplicate_block,
    unknown_parent,
    invalid_height,
    zero_block_work,
    accumulated_work_overflow
};

struct ChainIndexResult {
    ChainIndexError error = ChainIndexError::none;
    std::optional<ReorganizationPlan> reorganization;
};

class ChainIndex {
    std::unordered_map<Hash256, ChainEntry, Hash256Hasher> entries_;
    std::optional<Hash256> active_tip_;

    ReorganizationPlan plan_reorganization(Hash256 from, Hash256 to) const {
        ReorganizationPlan plan;
        const ChainEntry* old_entry = &entries_.at(from);
        const ChainEntry* new_entry = &entries_.at(to);
        while (old_entry->height > new_entry->height) {
            plan.disconnect.push_back(old_entry->id);
            old_entry = &entries_.at(old_entry->parent);
        }
        while (new_entry->height > old_entry->height) {
            plan.connect.push_back(new_entry->id);
            new_entry = &entries_.at(new_entry->parent);
        }
        while (old_entry->id != new_entry->id) {
            plan.disconnect.push_back(old_entry->id);
            plan.connect.push_back(new_entry->id);
            old_entry = &entries_.at(old_entry->parent);
            new_entry = &entries_.at(new_entry->parent);
        }
        std::reverse(plan.connect.begin(), plan.connect.end());
        return plan;
    }

public:
    ChainIndexError check_genesis(Hash256 id, ChainWork work) const {
        if (active_tip_) return ChainIndexError::genesis_already_exists;
        if (entries_.find(id) != entries_.end()) return ChainIndexError::duplicate_block;
        if (is_zero(work)) return ChainIndexError::zero_block_work;
        return ChainIndexError::none;
    }

    ChainIndexError check_block(Hash256 id, Hash256 parent, Height height,
                                ChainWork work) const {
        if (!active_tip_) return ChainIndexError::missing_genesis;
        if (entries_.find(id) != entries_.end()) return ChainIndexError::duplicate_block;
        const auto parent_iterator = entries_.find(parent);
        if (parent_iterator == entries_.end()) return ChainIndexError::unknown_parent;
        if (parent_iterator->second.height == std::numeric_limits<Height>::max() ||
            height != parent_iterator->second.height + 1U)
            return ChainIndexError::invalid_height;
        if (is_zero(work)) return ChainIndexError::zero_block_work;
        if (!add_chain_work(parent_iterator->second.accumulated_work, work))
            return ChainIndexError::accumulated_work_overflow;
        return ChainIndexError::none;
    }

    ChainIndexResult add_genesis(Hash256 id, ChainWork work,
                                 std::uint64_t timestamp) {
        const auto error = check_genesis(id, work);
        if (error != ChainIndexError::none) return {error, std::nullopt};
        entries_.emplace(id, ChainEntry{id, {}, 0U, work, work, timestamp});
        active_tip_ = id;
        return {};
    }

    ChainIndexResult add_block(Hash256 id, Hash256 parent, Height height,
                               ChainWork work, std::uint64_t timestamp) {
        const auto error = check_block(id, parent, height, work);
        if (error != ChainIndexError::none) return {error, std::nullopt};
        const auto parent_iterator = entries_.find(parent);
        const auto total = add_chain_work(parent_iterator->second.accumulated_work, work);
        // check_block already proved this cannot overflow.
        entries_.emplace(id, ChainEntry{id, parent, height, work, *total, timestamp});

        const auto& current = entries_.at(*active_tip_);
        if (!(current.accumulated_work < *total)) return {};
        auto plan = plan_reorganization(*active_tip_, id);
        active_tip_ = id;
        return {ChainIndexError::none, std::move(plan)};
    }

    const ChainEntry* find(const Hash256& id) const {
        const auto iterator = entries_.find(id);
        return iterator == entries_.end() ? nullptr : &iterator->second;
    }

    const ChainEntry* active_tip() const {
        return active_tip_ ? find(*active_tip_) : nullptr;
    }

    std::size_t size() const { return entries_.size(); }
};

} // namespace onuros
