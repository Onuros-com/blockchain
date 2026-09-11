#pragma once

#include "onuros/chain_index.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace onuros {

struct ActiveChainUndo {
    Hash256 disconnected;
    Hash256 restored_parent;
};

inline bool operator==(const ActiveChainUndo& left, const ActiveChainUndo& right) {
    return left.disconnected == right.disconnected &&
           left.restored_parent == right.restored_parent;
}

struct ActiveChainState {
    std::vector<Hash256> chain;
    std::vector<ActiveChainUndo> undo_log;

    std::optional<Hash256> tip() const {
        if (chain.empty()) return std::nullopt;
        return chain.back();
    }
};

enum class ReorganizationError {
    none,
    missing_entry,
    disconnect_order_mismatch,
    connect_parent_mismatch,
    connect_height_mismatch
};

inline ReorganizationError apply_reorganization_atomically(
        ActiveChainState& state, const ReorganizationPlan& plan,
        const ChainIndex& index) {
    auto virtual_size = state.chain.size();
    std::optional<Hash256> virtual_tip = state.tip();
    for (const auto& id : plan.disconnect) {
        const auto* entry = index.find(id);
        if (entry == nullptr) return ReorganizationError::missing_entry;
        if (virtual_size == 0U || state.chain[virtual_size - 1U] != id)
            return ReorganizationError::disconnect_order_mismatch;
        --virtual_size;
        virtual_tip = virtual_size == 0U
            ? std::optional<Hash256>{}
            : std::optional<Hash256>{state.chain[virtual_size - 1U]};
    }
    for (const auto& id : plan.connect) {
        const auto* entry = index.find(id);
        if (entry == nullptr) return ReorganizationError::missing_entry;
        const auto expected_height = static_cast<Height>(virtual_size);
        if (entry->height != expected_height)
            return ReorganizationError::connect_height_mismatch;
        if ((!virtual_tip && entry->parent != Hash256{}) ||
            (virtual_tip && entry->parent != *virtual_tip))
            return ReorganizationError::connect_parent_mismatch;
        ++virtual_size;
        virtual_tip = id;
    }

    state.chain.reserve(virtual_size);
    state.undo_log.reserve(state.undo_log.size() + plan.disconnect.size());
    for (const auto& id : plan.disconnect) {
        const auto* entry = index.find(id);
        state.undo_log.push_back({id, entry->parent});
        state.chain.pop_back();
    }
    state.chain.insert(state.chain.end(), plan.connect.begin(), plan.connect.end());
    return ReorganizationError::none;
}

inline std::optional<ActiveChainState> rebuild_active_chain(const ChainIndex& index) {
    ActiveChainState state;
    const auto* entry = index.active_tip();
    if (entry == nullptr) return state;
    while (true) {
        state.chain.push_back(entry->id);
        if (entry->height == 0U) break;
        entry = index.find(entry->parent);
        if (entry == nullptr) return std::nullopt;
    }
    std::reverse(state.chain.begin(), state.chain.end());
    for (std::size_t i = 0; i < state.chain.size(); ++i) {
        const auto* current = index.find(state.chain[i]);
        if (current == nullptr || current->height != i)
            return std::nullopt;
    }
    return state;
}

} // namespace onuros
