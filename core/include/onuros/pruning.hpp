#pragma once

#include "onuros/chain_index.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace onuros {

inline constexpr std::uint32_t pruning_checkpoint_version = 1U;

struct PruningPolicy {
    bool enabled = false;
    Height finality_depth = 0U;
    Height reorganization_window = 0U;
};

struct PruningCheckpoint {
    std::uint32_t version = pruning_checkpoint_version;
    Hash256 genesis{};
    Hash256 active_tip{};
    Hash256 shielded_root{};
    ChainWork accumulated_work{};
    Height tip_height = 0U;
    Height prune_below_height = 0U;
    Height finality_depth = 0U;
    Height reorganization_window = 0U;

    bool body_may_be_pruned(Height height) const noexcept {
        return height != 0U && height < prune_below_height;
    }
};

enum class PruningCheckpointError {
    none,
    disabled,
    unsafe_policy,
    invalid_version,
    wrong_genesis,
    tip_mismatch,
    shielded_root_mismatch,
    accumulated_work_mismatch,
    policy_mismatch,
    horizon_mismatch
};

struct PruningCheckpointResult {
    PruningCheckpointError error = PruningCheckpointError::none;
    std::optional<PruningCheckpoint> checkpoint;

    bool accepted() const noexcept {
        return error == PruningCheckpointError::none && checkpoint.has_value();
    }
};

namespace pruning_detail {

inline constexpr std::array<std::uint8_t, 8> checkpoint_magic{
    'O', 'N', 'P', 'R', 'U', 'N', 'E', '1'};
inline constexpr std::size_t checkpoint_payload_size =
    4U + 32U + 32U + 32U + 32U + 8U + 8U + 8U + 8U;
inline constexpr std::size_t checkpoint_encoded_size =
    checkpoint_magic.size() + checkpoint_payload_size + 32U;

template <typename Integer>
inline void append_little(std::vector<std::uint8_t>& output, Integer value) {
    for (std::size_t index = 0U; index < sizeof(Integer); ++index)
        output.push_back(
            static_cast<std::uint8_t>(value >> (index * 8U)));
}

inline void append_hash(std::vector<std::uint8_t>& output,
                        const Hash256& hash) {
    output.insert(output.end(), hash.begin(), hash.end());
}

class Reader {
    const std::vector<std::uint8_t>& input_;
    std::size_t offset_ = 0U;

public:
    explicit Reader(const std::vector<std::uint8_t>& input) : input_(input) {}

    template <typename Integer>
    bool little(Integer& value) {
        if (offset_ > input_.size() ||
            input_.size() - offset_ < sizeof(Integer))
            return false;
        value = 0;
        for (std::size_t index = 0U; index < sizeof(Integer); ++index)
            value |= static_cast<Integer>(input_[offset_ + index])
                     << (index * 8U);
        offset_ += sizeof(Integer);
        return true;
    }

    bool hash(Hash256& value) {
        if (offset_ > input_.size() || input_.size() - offset_ < value.size())
            return false;
        std::copy_n(input_.begin() + static_cast<std::ptrdiff_t>(offset_),
                    value.size(), value.begin());
        offset_ += value.size();
        return true;
    }

    bool exhausted() const noexcept { return offset_ == input_.size(); }
};

inline Height pruning_horizon(Height tip_height,
                              const PruningPolicy& policy) noexcept {
    const auto retained =
        std::max(policy.finality_depth, policy.reorganization_window);
    if (retained == 0U || tip_height == std::numeric_limits<Height>::max() ||
        tip_height + 1U <= retained)
        return 0U;
    return tip_height + 1U - retained;
}

} // namespace pruning_detail

inline PruningCheckpointResult make_pruning_checkpoint(
        const PruningPolicy& policy, Hash256 genesis,
        const ChainEntry& active_tip, Hash256 shielded_root) {
    if (!policy.enabled)
        return {PruningCheckpointError::disabled, std::nullopt};
    if (policy.finality_depth == 0U ||
        policy.reorganization_window == 0U)
        return {PruningCheckpointError::unsafe_policy, std::nullopt};
    PruningCheckpoint checkpoint;
    checkpoint.genesis = genesis;
    checkpoint.active_tip = active_tip.id;
    checkpoint.shielded_root = shielded_root;
    checkpoint.accumulated_work = active_tip.accumulated_work;
    checkpoint.tip_height = active_tip.height;
    checkpoint.prune_below_height =
        pruning_detail::pruning_horizon(active_tip.height, policy);
    checkpoint.finality_depth = policy.finality_depth;
    checkpoint.reorganization_window = policy.reorganization_window;
    return {PruningCheckpointError::none, checkpoint};
}

inline std::vector<std::uint8_t> encode_pruning_checkpoint(
        const PruningCheckpoint& checkpoint) {
    std::vector<std::uint8_t> output(
        pruning_detail::checkpoint_magic.begin(),
        pruning_detail::checkpoint_magic.end());
    output.reserve(pruning_detail::checkpoint_encoded_size);
    pruning_detail::append_little(output, checkpoint.version);
    pruning_detail::append_hash(output, checkpoint.genesis);
    pruning_detail::append_hash(output, checkpoint.active_tip);
    pruning_detail::append_hash(output, checkpoint.shielded_root);
    for (const auto limb : checkpoint.accumulated_work.limbs)
        pruning_detail::append_little(output, limb);
    pruning_detail::append_little(output, checkpoint.tip_height);
    pruning_detail::append_little(output, checkpoint.prune_below_height);
    pruning_detail::append_little(output, checkpoint.finality_depth);
    pruning_detail::append_little(output, checkpoint.reorganization_window);
    pruning_detail::append_hash(output, double_sha256(output));
    return output;
}

inline std::optional<PruningCheckpoint> decode_pruning_checkpoint(
        const std::vector<std::uint8_t>& encoded) {
    if (encoded.size() != pruning_detail::checkpoint_encoded_size ||
        !std::equal(pruning_detail::checkpoint_magic.begin(),
                    pruning_detail::checkpoint_magic.end(), encoded.begin()))
        return std::nullopt;
    const auto checksum_offset = encoded.size() - Hash256{}.size();
    const auto checksum = double_sha256(std::vector<std::uint8_t>(
        encoded.begin(),
        encoded.begin() + static_cast<std::ptrdiff_t>(checksum_offset)));
    if (!std::equal(checksum.begin(), checksum.end(),
                    encoded.begin() +
                        static_cast<std::ptrdiff_t>(checksum_offset)))
        return std::nullopt;
    const std::vector<std::uint8_t> payload(
        encoded.begin() + static_cast<std::ptrdiff_t>(
                              pruning_detail::checkpoint_magic.size()),
        encoded.begin() + static_cast<std::ptrdiff_t>(checksum_offset));
    pruning_detail::Reader reader(payload);
    PruningCheckpoint checkpoint;
    if (!reader.little(checkpoint.version) ||
        !reader.hash(checkpoint.genesis) ||
        !reader.hash(checkpoint.active_tip) ||
        !reader.hash(checkpoint.shielded_root))
        return std::nullopt;
    for (auto& limb : checkpoint.accumulated_work.limbs)
        if (!reader.little(limb)) return std::nullopt;
    if (!reader.little(checkpoint.tip_height) ||
        !reader.little(checkpoint.prune_below_height) ||
        !reader.little(checkpoint.finality_depth) ||
        !reader.little(checkpoint.reorganization_window) ||
        !reader.exhausted() ||
        checkpoint.version != pruning_checkpoint_version)
        return std::nullopt;
    return checkpoint;
}

inline PruningCheckpointError validate_pruning_checkpoint(
        const PruningCheckpoint& checkpoint, const PruningPolicy& policy,
        Hash256 genesis, const ChainEntry& active_tip,
        Hash256 shielded_root) {
    if (!policy.enabled) return PruningCheckpointError::disabled;
    if (policy.finality_depth == 0U ||
        policy.reorganization_window == 0U)
        return PruningCheckpointError::unsafe_policy;
    if (checkpoint.version != pruning_checkpoint_version)
        return PruningCheckpointError::invalid_version;
    if (checkpoint.genesis != genesis)
        return PruningCheckpointError::wrong_genesis;
    if (checkpoint.active_tip != active_tip.id ||
        checkpoint.tip_height != active_tip.height)
        return PruningCheckpointError::tip_mismatch;
    if (checkpoint.shielded_root != shielded_root)
        return PruningCheckpointError::shielded_root_mismatch;
    if (!(checkpoint.accumulated_work == active_tip.accumulated_work))
        return PruningCheckpointError::accumulated_work_mismatch;
    if (checkpoint.finality_depth != policy.finality_depth ||
        checkpoint.reorganization_window != policy.reorganization_window)
        return PruningCheckpointError::policy_mismatch;
    if (checkpoint.prune_below_height !=
        pruning_detail::pruning_horizon(active_tip.height, policy))
        return PruningCheckpointError::horizon_mismatch;
    return PruningCheckpointError::none;
}

} // namespace onuros
