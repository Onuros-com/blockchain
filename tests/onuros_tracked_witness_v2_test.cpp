#include "onuros/onuros_tracked_witness_backend.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>

struct onuros_privacy_tracked_witness_tree_v2 {
    std::vector<std::array<std::uint8_t, 32>> leaves;
    std::set<std::uint64_t> tracked;
    std::uint64_t rollback_limit = 0U;
};

struct onuros_privacy_tracked_witness_checkpoint_v2 {
    std::size_t leaves = 0U;
    std::set<std::uint64_t> tracked;
    bool used = false;
};

namespace {

using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

std::uint32_t abi_version = 2U;

std::uint32_t version() { return abi_version; }

onuros_privacy_tracked_witness_tree_v2* open_tree(
        std::uint64_t limit, onuros_privacy_status_v1* status) {
    if (status == nullptr) return nullptr;
    *status = ONUROS_PRIVACY_OK;
    auto* tree = new onuros_privacy_tracked_witness_tree_v2;
    tree->rollback_limit = limit;
    return tree;
}

void close_tree(onuros_privacy_tracked_witness_tree_v2* tree) { delete tree; }

onuros_privacy_status_v1 length(
        const onuros_privacy_tracked_witness_tree_v2* tree,
        std::uint64_t* leaves, std::uint64_t* tracked) {
    if (tree == nullptr || leaves == nullptr || tracked == nullptr)
        return ONUROS_PRIVACY_NULL_ARGUMENT;
    *leaves = tree->leaves.size();
    *tracked = tree->tracked.size();
    return ONUROS_PRIVACY_OK;
}

onuros_privacy_status_v1 append(
        onuros_privacy_tracked_witness_tree_v2* tree,
        const std::uint8_t* commitment, std::uint8_t track,
        std::uint64_t* position) {
    if (tree == nullptr || commitment == nullptr || position == nullptr)
        return ONUROS_PRIVACY_NULL_ARGUMENT;
    *position = 0U;
    if (track > 1U) return ONUROS_PRIVACY_INVALID_WITNESS;
    std::array<std::uint8_t, 32> value{};
    std::copy_n(commitment, value.size(), value.begin());
    *position = tree->leaves.size();
    tree->leaves.push_back(value);
    if (track == 1U) tree->tracked.insert(*position);
    return ONUROS_PRIVACY_OK;
}

onuros_privacy_status_v1 untrack(
        onuros_privacy_tracked_witness_tree_v2* tree,
        std::uint64_t position) {
    if (tree == nullptr) return ONUROS_PRIVACY_NULL_ARGUMENT;
    return tree->tracked.erase(position) == 1U
        ? ONUROS_PRIVACY_OK : ONUROS_PRIVACY_WITNESS_POSITION_UNKNOWN;
}

onuros_privacy_status_v1 root(
        const onuros_privacy_tracked_witness_tree_v2* tree,
        std::uint8_t* output) {
    if (tree == nullptr || output == nullptr)
        return ONUROS_PRIVACY_NULL_ARGUMENT;
    std::fill_n(output, 32U, static_cast<std::uint8_t>(0U));
    output[0] = static_cast<std::uint8_t>(tree->leaves.size());
    for (const auto& leaf : tree->leaves) output[1] ^= leaf[0];
    return ONUROS_PRIVACY_OK;
}

onuros_privacy_status_v1 path(
        const onuros_privacy_tracked_witness_tree_v2* tree,
        std::uint64_t position, std::uint8_t* root_output,
        std::uint8_t (*siblings)[32], std::uint8_t* right) {
    if (tree == nullptr || root_output == nullptr || siblings == nullptr ||
        right == nullptr) return ONUROS_PRIVACY_NULL_ARGUMENT;
    std::fill_n(root_output, 32U, 0U);
    for (std::size_t level = 0U; level < 32U; ++level) {
        std::fill_n(siblings[level], 32U, 0U);
        right[level] = 0U;
    }
    if (tree->tracked.count(position) == 0U)
        return ONUROS_PRIVACY_WITNESS_POSITION_UNKNOWN;
    root_output[0] = static_cast<std::uint8_t>(tree->leaves.size());
    siblings[0][0] = tree->leaves[position][0];
    return ONUROS_PRIVACY_OK;
}

onuros_privacy_tracked_witness_checkpoint_v2* checkpoint_open(
        const onuros_privacy_tracked_witness_tree_v2* tree,
        onuros_privacy_status_v1* status) {
    if (tree == nullptr || status == nullptr) return nullptr;
    *status = ONUROS_PRIVACY_OK;
    auto* checkpoint = new onuros_privacy_tracked_witness_checkpoint_v2;
    checkpoint->leaves = tree->leaves.size();
    checkpoint->tracked = tree->tracked;
    return checkpoint;
}

void checkpoint_close(onuros_privacy_tracked_witness_checkpoint_v2* value) {
    delete value;
}

onuros_privacy_status_v1 rollback(
        onuros_privacy_tracked_witness_tree_v2* tree,
        const onuros_privacy_tracked_witness_checkpoint_v2* checkpoint) {
    if (tree == nullptr || checkpoint == nullptr)
        return ONUROS_PRIVACY_NULL_ARGUMENT;
    auto* mutable_checkpoint =
        const_cast<onuros_privacy_tracked_witness_checkpoint_v2*>(checkpoint);
    if (mutable_checkpoint->used || checkpoint->leaves > tree->leaves.size())
        return ONUROS_PRIVACY_INVALID_CHECKPOINT;
    if (tree->leaves.size() - checkpoint->leaves > tree->rollback_limit)
        return ONUROS_PRIVACY_ROLLBACK_TOO_DEEP;
    tree->leaves.resize(checkpoint->leaves);
    tree->tracked = checkpoint->tracked;
    mutable_checkpoint->used = true;
    return ONUROS_PRIVACY_OK;
}

constexpr std::size_t snapshot_header = 18U;

onuros_privacy_status_v1 snapshot_size(
        const onuros_privacy_tracked_witness_tree_v2* tree,
        const onuros_tracked_witness_binding_v2*, std::size_t* size) {
    if (tree == nullptr || size == nullptr) return ONUROS_PRIVACY_NULL_ARGUMENT;
    *size = snapshot_header + tree->leaves.size();
    return ONUROS_PRIVACY_OK;
}

onuros_privacy_status_v1 snapshot_export(
        const onuros_privacy_tracked_witness_tree_v2* tree,
        const onuros_tracked_witness_binding_v2* binding,
        std::uint8_t* output, std::size_t capacity, std::size_t* written) {
    if (tree == nullptr || binding == nullptr || output == nullptr ||
        written == nullptr) return ONUROS_PRIVACY_NULL_ARGUMENT;
    *written = 0U;
    const auto required = snapshot_header + tree->leaves.size();
    if (capacity < required) return ONUROS_PRIVACY_OUTPUT_TOO_SMALL;
    std::fill_n(output, capacity, 0U);
    output[0] = static_cast<std::uint8_t>(binding->network_id);
    output[1] = static_cast<std::uint8_t>(binding->circuit_version);
    output[2] = static_cast<std::uint8_t>(binding->block_height);
    output[3] = binding->block_hash[0];
    output[4] = binding->note_root[0];
    output[5] = static_cast<std::uint8_t>(tree->leaves.size());
    for (std::size_t index = 0U; index < tree->leaves.size(); ++index)
        output[snapshot_header + index] = tree->leaves[index][0];
    *written = required;
    return ONUROS_PRIVACY_OK;
}

onuros_privacy_tracked_witness_tree_v2* snapshot_import(
        const std::uint8_t* encoded, std::size_t size,
        const onuros_tracked_witness_binding_v2* binding,
        const onuros_tracked_witness_limits_v2* limits,
        onuros_privacy_status_v1* status) {
    if (encoded == nullptr || binding == nullptr || limits == nullptr ||
        status == nullptr) return nullptr;
    if (size > limits->max_snapshot_bytes) {
        *status = ONUROS_PRIVACY_SNAPSHOT_TOO_LARGE;
        return nullptr;
    }
    if (size < snapshot_header || encoded[0] != binding->network_id ||
        encoded[1] != binding->circuit_version ||
        encoded[2] != binding->block_height ||
        encoded[3] != binding->block_hash[0] ||
        encoded[4] != binding->note_root[0]) {
        *status = ONUROS_PRIVACY_SNAPSHOT_BINDING_MISMATCH;
        return nullptr;
    }
    const auto count = encoded[5];
    if (size != snapshot_header + count) {
        *status = ONUROS_PRIVACY_INVALID_SNAPSHOT;
        return nullptr;
    }
    auto* tree = new onuros_privacy_tracked_witness_tree_v2;
    tree->rollback_limit = limits->max_rollback_leaves;
    for (std::size_t index = 0U; index < count; ++index) {
        std::array<std::uint8_t, 32> leaf{};
        leaf[0] = encoded[snapshot_header + index];
        tree->leaves.push_back(leaf);
    }
    *status = ONUROS_PRIVACY_OK;
    return tree;
}

TrackedWitnessV2Api api() {
    return {&version, &open_tree, &close_tree, &length, &append, &untrack,
            &root, &path, &checkpoint_open, &checkpoint_close, &rollback,
            &snapshot_size, &snapshot_export, &snapshot_import};
}

onuros_tracked_witness_binding_v2 binding() {
    onuros_tracked_witness_binding_v2 value{};
    value.network_id = 7U;
    value.circuit_version = 1U;
    value.block_height = 9U;
    value.block_hash[0] = 10U;
    value.note_root[0] = 11U;
    return value;
}

} // namespace

int main() {
    onuros_privacy_status_v1 status = ONUROS_PRIVACY_INTERNAL_PANIC;
    abi_version = 1U;
    check(OnurosTrackedWitnessV2::open(api(), 4U, status) == nullptr);
    check(status == ONUROS_PRIVACY_INVALID_PARAMETERS);
    abi_version = 2U;

    auto witness = OnurosTrackedWitnessV2::open(api(), 4U, status);
    check(witness != nullptr && status == ONUROS_PRIVACY_OK);
    std::array<std::uint8_t, 32> first{};
    first[0] = 3U;
    std::uint64_t position = 99U;
    check(witness->append(first, true, position) == ONUROS_PRIVACY_OK);
    check(position == 0U);
    std::uint64_t leaves = 0U;
    std::uint64_t tracked = 0U;
    check(witness->length(leaves, tracked) == ONUROS_PRIVACY_OK);
    check(leaves == 1U && tracked == 1U);
    std::array<std::uint8_t, 32> path_root{};
    std::array<std::array<std::uint8_t, 32>, 32> siblings{};
    std::array<std::uint8_t, 32> right{};
    check(witness->path(0U, path_root, siblings, right) == ONUROS_PRIVACY_OK);
    check(path_root[0] == 1U && siblings[0][0] == 3U);
    check(witness->path(99U, path_root, siblings, right) ==
          ONUROS_PRIVACY_WITNESS_POSITION_UNKNOWN);
    check(path_root == std::array<std::uint8_t, 32>{});

    auto checkpoint = witness->checkpoint(status);
    check(checkpoint != nullptr && status == ONUROS_PRIVACY_OK);
    std::array<std::uint8_t, 32> second{};
    second[0] = 5U;
    check(witness->append(second, false, position) == ONUROS_PRIVACY_OK);
    check(witness->rollback(*checkpoint) == ONUROS_PRIVACY_OK);
    check(witness->rollback(*checkpoint) == ONUROS_PRIVACY_INVALID_CHECKPOINT);
    check(witness->length(leaves, tracked) == ONUROS_PRIVACY_OK);
    check(leaves == 1U && tracked == 1U);

    std::vector<std::uint8_t> snapshot{static_cast<std::uint8_t>(99U)};
    check(witness->export_snapshot(binding(), 1U, snapshot) ==
          ONUROS_PRIVACY_SNAPSHOT_TOO_LARGE);
    check(snapshot.empty());
    check(witness->export_snapshot(binding(), 1024U, snapshot) ==
          ONUROS_PRIVACY_OK);

    const onuros_tracked_witness_limits_v2 limits{
        1024U, 8U, 256U, 4U};
    auto restored = OnurosTrackedWitnessV2::import_snapshot(
        api(), snapshot, binding(), limits, status);
    check(restored != nullptr && status == ONUROS_PRIVACY_OK);
    check(restored->length(leaves, tracked) == ONUROS_PRIVACY_OK);
    check(leaves == 1U);

    auto wrong_binding = binding();
    ++wrong_binding.network_id;
    check(OnurosTrackedWitnessV2::import_snapshot(
              api(), snapshot, wrong_binding, limits, status) == nullptr);
    check(status == ONUROS_PRIVACY_SNAPSHOT_BINDING_MISMATCH);

    auto restrictive = limits;
    restrictive.max_snapshot_bytes = snapshot.size() - 1U;
    check(OnurosTrackedWitnessV2::import_snapshot(
              api(), snapshot, binding(), restrictive, status) == nullptr);
    check(status == ONUROS_PRIVACY_SNAPSHOT_TOO_LARGE);
    return 0;
}
