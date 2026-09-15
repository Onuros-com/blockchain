#pragma once

#include "onuros/onuros_tracked_witness_backend.hpp"
#include "onuros/private_block.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace onuros {

// Consensus-facing Poseidon note-root calculator backed by Privacy Engine
// tracked-witness ABI v2. Replaying the public commitments makes this adapter
// stateless and deterministic. A later optimization may reuse an authenticated
// snapshot, but it must preserve this exact result.
class OnurosPoseidonRootCalculator final : public ShieldedRootCalculator {
    TrackedWitnessV2Api api_;
    std::uint64_t maximum_rollback_leaves_;

public:
    explicit OnurosPoseidonRootCalculator(
            TrackedWitnessV2Api api,
            std::uint64_t maximum_rollback_leaves = 1'000'000U)
        : api_(api), maximum_rollback_leaves_(maximum_rollback_leaves) {}

    std::optional<Hash256> calculate(
            const std::vector<Hash256>& active_commitments,
            const std::vector<Hash256>& new_commitments) const override {
        onuros_privacy_status_v1 status = ONUROS_PRIVACY_INTERNAL_PANIC;
        auto tree = OnurosTrackedWitnessV2::open(
            api_, maximum_rollback_leaves_, status);
        if (!tree || status != ONUROS_PRIVACY_OK) return std::nullopt;
        std::uint64_t position = 0U;
        for (const auto& commitment : active_commitments)
            if (tree->append(commitment, false, position) != ONUROS_PRIVACY_OK)
                return std::nullopt;
        for (const auto& commitment : new_commitments)
            if (tree->append(commitment, false, position) != ONUROS_PRIVACY_OK)
                return std::nullopt;
        Hash256 root{};
        if (tree->root(root) != ONUROS_PRIVACY_OK) return std::nullopt;
        return root;
    }

#ifdef ONUROS_PRIVACY_ENGINE_ENABLED
    static OnurosPoseidonRootCalculator linked(
            std::uint64_t maximum_rollback_leaves = 1'000'000U) {
        return OnurosPoseidonRootCalculator(
            OnurosTrackedWitnessV2::linked_api(), maximum_rollback_leaves);
    }
#endif
};

} // namespace onuros
