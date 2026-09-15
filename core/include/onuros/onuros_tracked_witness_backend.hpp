#pragma once

#include "onuros/onuros_privacy_tracked_witness_v2.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace onuros {

// The tracked-witness ABI is wallet/state recovery support. It does not replace
// consensus nullifier or note-root validation and must be bound to an
// authenticated header-chain view before importing a snapshot.
struct TrackedWitnessV2Api {
    using Tree = onuros_privacy_tracked_witness_tree_v2;
    using Checkpoint = onuros_privacy_tracked_witness_checkpoint_v2;

    std::uint32_t (*version)() = nullptr;
    Tree* (*open)(std::uint64_t, onuros_privacy_status_v1*) = nullptr;
    void (*close)(Tree*) = nullptr;
    onuros_privacy_status_v1 (*len)(const Tree*, std::uint64_t*,
                                    std::uint64_t*) = nullptr;
    onuros_privacy_status_v1 (*append)(Tree*, const std::uint8_t*,
                                       std::uint8_t, std::uint64_t*) = nullptr;
    onuros_privacy_status_v1 (*untrack)(Tree*, std::uint64_t) = nullptr;
    onuros_privacy_status_v1 (*root)(const Tree*, std::uint8_t*) = nullptr;
    onuros_privacy_status_v1 (*path)(const Tree*, std::uint64_t,
                                     std::uint8_t*,
                                     std::uint8_t (*)[32],
                                     std::uint8_t*) = nullptr;
    Checkpoint* (*checkpoint_open)(const Tree*,
                                   onuros_privacy_status_v1*) = nullptr;
    void (*checkpoint_close)(Checkpoint*) = nullptr;
    onuros_privacy_status_v1 (*rollback)(Tree*, const Checkpoint*) = nullptr;
    onuros_privacy_status_v1 (*snapshot_size)(
        const Tree*, const onuros_tracked_witness_binding_v2*,
        std::size_t*) = nullptr;
    onuros_privacy_status_v1 (*snapshot_export)(
        const Tree*, const onuros_tracked_witness_binding_v2*, std::uint8_t*,
        std::size_t, std::size_t*) = nullptr;
    Tree* (*snapshot_import)(
        const std::uint8_t*, std::size_t,
        const onuros_tracked_witness_binding_v2*,
        const onuros_tracked_witness_limits_v2*,
        onuros_privacy_status_v1*) = nullptr;

    bool complete() const noexcept {
        return version != nullptr && open != nullptr && close != nullptr &&
            len != nullptr && append != nullptr && untrack != nullptr &&
            root != nullptr && path != nullptr && checkpoint_open != nullptr &&
            checkpoint_close != nullptr && rollback != nullptr &&
            snapshot_size != nullptr && snapshot_export != nullptr &&
            snapshot_import != nullptr;
    }
};

class OnurosTrackedWitnessV2 final {
public:
    using Tree = TrackedWitnessV2Api::Tree;
    using RawCheckpoint = TrackedWitnessV2Api::Checkpoint;

    class Checkpoint final {
        const TrackedWitnessV2Api* api_ = nullptr;
        RawCheckpoint* checkpoint_ = nullptr;
        friend class OnurosTrackedWitnessV2;

        Checkpoint(const TrackedWitnessV2Api& api, RawCheckpoint* checkpoint)
            : api_(&api), checkpoint_(checkpoint) {}

    public:
        ~Checkpoint() {
            if (checkpoint_ != nullptr) api_->checkpoint_close(checkpoint_);
        }
        Checkpoint(const Checkpoint&) = delete;
        Checkpoint& operator=(const Checkpoint&) = delete;
        const RawCheckpoint* raw() const noexcept { return checkpoint_; }
    };

private:
    TrackedWitnessV2Api api_{};
    Tree* tree_ = nullptr;

    OnurosTrackedWitnessV2(TrackedWitnessV2Api api, Tree* tree)
        : api_(api), tree_(tree) {}

public:
    ~OnurosTrackedWitnessV2() {
        if (tree_ != nullptr) api_.close(tree_);
    }
    OnurosTrackedWitnessV2(const OnurosTrackedWitnessV2&) = delete;
    OnurosTrackedWitnessV2& operator=(const OnurosTrackedWitnessV2&) = delete;

    static std::unique_ptr<OnurosTrackedWitnessV2> open(
            TrackedWitnessV2Api api, std::uint64_t max_rollback_leaves,
            onuros_privacy_status_v1& status) {
        status = ONUROS_PRIVACY_INVALID_PARAMETERS;
        if (!api.complete() || api.version() != 2U) return nullptr;
        auto* tree = api.open(max_rollback_leaves, &status);
        if (tree == nullptr || status != ONUROS_PRIVACY_OK) {
            if (tree != nullptr) api.close(tree);
            return nullptr;
        }
        return std::unique_ptr<OnurosTrackedWitnessV2>(
            new OnurosTrackedWitnessV2(api, tree));
    }

    static std::unique_ptr<OnurosTrackedWitnessV2> import_snapshot(
            TrackedWitnessV2Api api, const std::vector<std::uint8_t>& encoded,
            const onuros_tracked_witness_binding_v2& binding,
            const onuros_tracked_witness_limits_v2& limits,
            onuros_privacy_status_v1& status) {
        status = ONUROS_PRIVACY_INVALID_PARAMETERS;
        if (!api.complete() || api.version() != 2U)
            return nullptr;
        if (encoded.empty()) {
            status = ONUROS_PRIVACY_INVALID_SNAPSHOT;
            return nullptr;
        }
        if (encoded.size() > limits.max_snapshot_bytes) {
            status = ONUROS_PRIVACY_SNAPSHOT_TOO_LARGE;
            return nullptr;
        }
        auto* tree = api.snapshot_import(encoded.data(), encoded.size(),
                                         &binding, &limits, &status);
        if (tree == nullptr || status != ONUROS_PRIVACY_OK) {
            if (tree != nullptr) api.close(tree);
            return nullptr;
        }
        return std::unique_ptr<OnurosTrackedWitnessV2>(
            new OnurosTrackedWitnessV2(api, tree));
    }

    onuros_privacy_status_v1 length(std::uint64_t& leaves,
                                    std::uint64_t& tracked) const {
        leaves = 0U;
        tracked = 0U;
        return api_.len(tree_, &leaves, &tracked);
    }

    onuros_privacy_status_v1 append(const std::array<std::uint8_t, 32>& value,
                                    bool track, std::uint64_t& position) {
        position = 0U;
        return api_.append(tree_, value.data(), track ? 1U : 0U, &position);
    }

    onuros_privacy_status_v1 untrack(std::uint64_t position) {
        return api_.untrack(tree_, position);
    }

    onuros_privacy_status_v1 root(
            std::array<std::uint8_t, 32>& output) const {
        output.fill(0U);
        return api_.root(tree_, output.data());
    }

    onuros_privacy_status_v1 path(
            std::uint64_t position,
            std::array<std::uint8_t, 32>& root,
            std::array<std::array<std::uint8_t, 32>, 32>& siblings,
            std::array<std::uint8_t, 32>& right) const {
        root.fill(0U);
        siblings.fill({});
        right.fill(0U);
        return api_.path(tree_, position, root.data(),
                         reinterpret_cast<std::uint8_t (*)[32]>(
                             siblings.data()),
                         right.data());
    }

    std::unique_ptr<Checkpoint> checkpoint(
            onuros_privacy_status_v1& status) const {
        status = ONUROS_PRIVACY_INTERNAL_PANIC;
        auto* checkpoint = api_.checkpoint_open(tree_, &status);
        if (checkpoint == nullptr || status != ONUROS_PRIVACY_OK) {
            if (checkpoint != nullptr) api_.checkpoint_close(checkpoint);
            return nullptr;
        }
        return std::unique_ptr<Checkpoint>(new Checkpoint(api_, checkpoint));
    }

    onuros_privacy_status_v1 rollback(const Checkpoint& checkpoint) {
        return api_.rollback(tree_, checkpoint.raw());
    }

    onuros_privacy_status_v1 export_snapshot(
            const onuros_tracked_witness_binding_v2& binding,
            std::size_t maximum_bytes,
            std::vector<std::uint8_t>& output) const {
        output.clear();
        std::size_t size = 0U;
        auto status = api_.snapshot_size(tree_, &binding, &size);
        if (status != ONUROS_PRIVACY_OK) return status;
        if (size == 0U || size > maximum_bytes)
            return ONUROS_PRIVACY_SNAPSHOT_TOO_LARGE;
        std::vector<std::uint8_t> candidate(size);
        std::size_t written = 0U;
        status = api_.snapshot_export(tree_, &binding, candidate.data(),
                                      candidate.size(), &written);
        if (status != ONUROS_PRIVACY_OK || written != candidate.size())
            return status == ONUROS_PRIVACY_OK
                ? ONUROS_PRIVACY_INVALID_SNAPSHOT : status;
        output = std::move(candidate);
        return ONUROS_PRIVACY_OK;
    }

#ifdef ONUROS_PRIVACY_ENGINE_ENABLED
    static TrackedWitnessV2Api linked_api() {
        return {
            &onuros_privacy_tracked_witness_abi_version_v2,
            &onuros_privacy_tracked_witness_open_v2,
            &onuros_privacy_tracked_witness_close_v2,
            &onuros_privacy_tracked_witness_len_v2,
            &onuros_privacy_tracked_witness_append_v2,
            &onuros_privacy_tracked_witness_untrack_v2,
            &onuros_privacy_tracked_witness_root_v2,
            &onuros_privacy_tracked_witness_path_v2,
            &onuros_privacy_tracked_witness_checkpoint_open_v2,
            &onuros_privacy_tracked_witness_checkpoint_close_v2,
            &onuros_privacy_tracked_witness_rollback_v2,
            &onuros_privacy_tracked_witness_snapshot_size_v2,
            &onuros_privacy_tracked_witness_export_v2,
            &onuros_privacy_tracked_witness_import_v2};
    }
#endif
};

} // namespace onuros
