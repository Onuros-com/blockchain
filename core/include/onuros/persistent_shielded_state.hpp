#pragma once

#include "onuros/block_store.hpp"
#include "onuros/pruning.hpp"
#include "onuros/private_admission.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

namespace onuros {

enum class ShieldedStoreError {
    none,
    not_open,
    io_error,
    database_too_large,
    corrupt_database,
    wrong_genesis,
    unsafe_retention,
    checkpoint_mismatch,
    state_transition_failed
};

struct ShieldedConnect {
    Hash256 block_id{};
    Hash256 resulting_root{};
    PrivateBlockAdmission::Prepared prepared;
};

namespace shielded_store_detail {

inline constexpr std::array<std::uint8_t, 8> magic{
    'O', 'N', 'U', 'R', 'S', 'H', '0', '2'};
inline constexpr std::array<std::uint8_t, 8> legacy_magic{
    'O', 'N', 'U', 'R', 'S', 'H', '0', '1'};

inline void append_hashes(std::vector<std::uint8_t>& output,
                          const std::vector<Hash256>& hashes) {
    if (hashes.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("too many shielded state entries");
    detail::append_u32(output, static_cast<std::uint32_t>(hashes.size()));
    for (const auto& hash : hashes) detail::append_hash(output, hash);
}

inline std::vector<std::uint8_t> encode(const ShieldedSnapshot& snapshot) {
    std::vector<std::uint8_t> payload;
    detail::append_hash(payload, snapshot.genesis_block);
    detail::append_hash(payload, snapshot.genesis_root);
    detail::append_hash(payload, snapshot.tip_block);
    detail::append_hash(payload, snapshot.current_root);
    append_hashes(payload, snapshot.nullifiers);
    append_hashes(payload, snapshot.commitments);
    append_hashes(payload, snapshot.ordered_commitments);
    append_hashes(payload, snapshot.ordered_roots);
    detail::append_u64(payload, snapshot.height);
    detail::append_u64(payload, snapshot.history_base_height);
    detail::append_hash(payload, snapshot.history_base_block);
    detail::append_hash(payload, snapshot.history_base_root);
    detail::append_u64(payload, snapshot.undo_retention_limit);
    if (snapshot.history.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("too many shielded undo entries");
    detail::append_u32(payload, static_cast<std::uint32_t>(snapshot.history.size()));
    for (const auto& undo : snapshot.history) {
        detail::append_hash(payload, undo.block_id);
        detail::append_hash(payload, undo.parent_block);
        detail::append_hash(payload, undo.previous_root);
        detail::append_hash(payload, undo.resulting_root);
        append_hashes(payload, undo.nullifiers);
        append_hashes(payload, undo.commitments);
    }
    std::vector<std::uint8_t> bytes(magic.begin(), magic.end());
    if (payload.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("shielded state snapshot too large");
    detail::append_u32(bytes, static_cast<std::uint32_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    detail::append_hash(bytes, double_sha256(bytes));
    return bytes;
}

class Reader {
    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_ = 0U;
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
    std::size_t remaining() const { return bytes_.size() - offset_; }
    bool hash(Hash256& value) {
        if (remaining() < value.size()) return false;
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                    value.size(), value.begin());
        offset_ += value.size();
        return true;
    }
    bool u32(std::uint32_t& value) {
        return detail::take_u32(bytes_, offset_, value);
    }
    bool u64(std::uint64_t& value) {
        return detail::take_u64(bytes_, offset_, value);
    }
    bool hashes(std::vector<Hash256>& values, std::size_t maximum) {
        std::uint32_t count = 0U;
        if (!u32(count) || count > maximum ||
            count > remaining() / Hash256{}.size()) return false;
        values.resize(count);
        for (auto& value : values) if (!hash(value)) return false;
        return true;
    }
};

inline std::optional<ShieldedSnapshot> decode(
        const std::vector<std::uint8_t>& bytes, std::size_t maximum_entries) {
    constexpr std::size_t framing = magic.size() + 4U + 32U;
    if (bytes.size() < framing)
        return std::nullopt;
    const bool version_two =
        std::equal(magic.begin(), magic.end(), bytes.begin());
    const bool version_one =
        std::equal(legacy_magic.begin(), legacy_magic.end(), bytes.begin());
    if (!version_two && !version_one) return std::nullopt;
    std::size_t size_offset = magic.size();
    std::uint32_t payload_size = 0U;
    if (!detail::take_u32(bytes, size_offset, payload_size) ||
        payload_size != bytes.size() - framing)
        return std::nullopt;
    const auto checksum_start = bytes.size() - 32U;
    const auto checksum = double_sha256(std::vector<std::uint8_t>(
        bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(checksum_start)));
    if (!std::equal(checksum.begin(), checksum.end(),
                    bytes.begin() + static_cast<std::ptrdiff_t>(checksum_start)))
        return std::nullopt;
    const std::vector<std::uint8_t> payload(
        bytes.begin() + static_cast<std::ptrdiff_t>(size_offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(checksum_start));
    Reader reader(payload);
    ShieldedSnapshot snapshot;
    if (!reader.hash(snapshot.genesis_block) ||
        !reader.hash(snapshot.genesis_root) ||
        !reader.hash(snapshot.tip_block) ||
        !reader.hash(snapshot.current_root) ||
        !reader.hashes(snapshot.nullifiers, maximum_entries) ||
        !reader.hashes(snapshot.commitments, maximum_entries))
        return std::nullopt;
    if (version_two &&
        (!reader.hashes(snapshot.ordered_commitments, maximum_entries) ||
         !reader.hashes(snapshot.ordered_roots, maximum_entries) ||
         !reader.u64(snapshot.height) ||
         !reader.u64(snapshot.history_base_height) ||
         !reader.hash(snapshot.history_base_block) ||
         !reader.hash(snapshot.history_base_root) ||
         !reader.u64(snapshot.undo_retention_limit)))
        return std::nullopt;
    std::uint32_t history_size = 0U;
    if (!reader.u32(history_size) || history_size > maximum_entries)
        return std::nullopt;
    if (snapshot.nullifiers.size() > maximum_entries ||
        snapshot.commitments.size() >
            maximum_entries - snapshot.nullifiers.size())
        return std::nullopt;
    snapshot.history.resize(history_size);
    std::size_t total_entries = snapshot.nullifiers.size() +
                                snapshot.commitments.size();
    if (snapshot.ordered_commitments.size() > maximum_entries - total_entries)
        return std::nullopt;
    total_entries += snapshot.ordered_commitments.size();
    if (snapshot.ordered_roots.size() > maximum_entries - total_entries)
        return std::nullopt;
    total_entries += snapshot.ordered_roots.size();
    for (auto& undo : snapshot.history) {
        if (!reader.hash(undo.block_id) || !reader.hash(undo.parent_block) ||
            !reader.hash(undo.previous_root) ||
            !reader.hash(undo.resulting_root) ||
            !reader.hashes(undo.nullifiers, maximum_entries) ||
            !reader.hashes(undo.commitments, maximum_entries) ||
            total_entries > maximum_entries ||
            undo.nullifiers.size() > maximum_entries - total_entries)
            return std::nullopt;
        total_entries += undo.nullifiers.size();
        if (undo.commitments.size() > maximum_entries - total_entries)
            return std::nullopt;
        total_entries += undo.commitments.size();
    }
    if (version_one) {
        snapshot.height = static_cast<std::uint64_t>(snapshot.history.size());
        snapshot.history_base_height = 0U;
        snapshot.history_base_block = snapshot.genesis_block;
        snapshot.history_base_root = snapshot.genesis_root;
        snapshot.undo_retention_limit =
            std::numeric_limits<std::uint64_t>::max();
        snapshot.ordered_roots.push_back(snapshot.genesis_root);
        for (const auto& undo : snapshot.history) {
            snapshot.ordered_commitments.insert(
                snapshot.ordered_commitments.end(), undo.commitments.begin(),
                undo.commitments.end());
            snapshot.ordered_roots.push_back(undo.resulting_root);
        }
        if (snapshot.ordered_commitments.size() >
            maximum_entries - total_entries)
            return std::nullopt;
        total_entries += snapshot.ordered_commitments.size();
        if (snapshot.ordered_roots.size() > maximum_entries - total_entries)
            return std::nullopt;
    }
    if (reader.remaining() != 0U) return std::nullopt;
    return snapshot;
}

} // namespace shielded_store_detail

class PersistentShieldedState {
    std::filesystem::path path_;
    std::size_t max_bytes_;
    std::size_t max_entries_;
    Hash256 expected_genesis_{};
    Hash256 expected_root_{};
    std::vector<Hash256> expected_initial_commitments_;
    ShieldedState state_;
    bool open_ = false;

    ShieldedStoreError persist(const ShieldedState& state) const {
        try {
            const auto bytes = shielded_store_detail::encode(state.snapshot());
            if (bytes.size() > max_bytes_)
                return ShieldedStoreError::database_too_large;
            if (!detail::create_atomic(path_, bytes))
                return ShieldedStoreError::io_error;
            return ShieldedStoreError::none;
        } catch (const std::length_error&) {
            return ShieldedStoreError::database_too_large;
        }
    }

public:
    PersistentShieldedState(Hash256 genesis, Hash256 initial_root,
                            std::size_t max_bytes,
                            std::size_t max_entries)
        : max_bytes_(max_bytes), max_entries_(max_entries),
          expected_genesis_(genesis), expected_root_(initial_root),
          state_(genesis, initial_root) {}

    PersistentShieldedState(Hash256 genesis, Hash256 initial_root,
                            std::vector<Hash256> initial_commitments,
                            std::size_t max_bytes,
                            std::size_t max_entries)
        : max_bytes_(max_bytes), max_entries_(max_entries),
          expected_genesis_(genesis), expected_root_(initial_root),
          expected_initial_commitments_(initial_commitments),
          state_(genesis, initial_root, std::move(initial_commitments)) {}

    ShieldedStoreError open(const std::filesystem::path& path) {
        path_ = path;
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            if (error) return ShieldedStoreError::io_error;
            const auto result = persist(state_);
            if (result != ShieldedStoreError::none) return result;
            open_ = true;
            return ShieldedStoreError::none;
        }
        const auto size = std::filesystem::file_size(path, error);
        if (error) return ShieldedStoreError::io_error;
        if (size > max_bytes_ ||
            size > static_cast<std::uintmax_t>(
                       std::numeric_limits<std::size_t>::max()))
            return ShieldedStoreError::database_too_large;
        std::ifstream input(path, std::ios::binary);
        if (!input) return ShieldedStoreError::io_error;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input && !bytes.empty()) return ShieldedStoreError::io_error;
        const auto snapshot = shielded_store_detail::decode(bytes, max_entries_);
        if (!snapshot) return ShieldedStoreError::corrupt_database;
        if (snapshot->genesis_block != expected_genesis_ ||
            snapshot->genesis_root != expected_root_)
            return ShieldedStoreError::wrong_genesis;
        if (!expected_initial_commitments_.empty() &&
            (snapshot->ordered_commitments.size() <
                 expected_initial_commitments_.size() ||
             !std::equal(expected_initial_commitments_.begin(),
                         expected_initial_commitments_.end(),
                         snapshot->ordered_commitments.begin())))
            return ShieldedStoreError::wrong_genesis;
        auto restored = ShieldedState::restore(*snapshot);
        if (!restored) return ShieldedStoreError::corrupt_database;
        state_ = std::move(*restored);
        open_ = true;
        return ShieldedStoreError::none;
    }

    ShieldedStoreError reorg(
            const std::vector<Hash256>& disconnect,
            const std::vector<ShieldedConnect>& connect) {
        if (!open_) return ShieldedStoreError::not_open;
        auto next = state_;
        for (const auto& block : disconnect)
            if (next.disconnect(block) != PrivateAdmissionError::none)
                return ShieldedStoreError::state_transition_failed;
        for (const auto& block : connect)
            if (next.connect(block.block_id, block.resulting_root,
                             block.prepared) != PrivateAdmissionError::none)
                return ShieldedStoreError::state_transition_failed;
        const auto result = persist(next);
        if (result != ShieldedStoreError::none) return result;
        state_ = std::move(next);
        return ShieldedStoreError::none;
    }

    ShieldedStoreError connect(
            const Hash256& block_id, const Hash256& resulting_root,
            const PrivateBlockAdmission::Prepared& prepared) {
        return reorg({}, {{block_id, resulting_root, prepared}});
    }

    ShieldedStoreError disconnect(const Hash256& block_id) {
        return reorg({block_id}, {});
    }

    ShieldedStoreError retain_undo_history(
            const PruningCheckpoint& checkpoint) {
        if (!open_) return ShieldedStoreError::not_open;
        if (checkpoint.version != pruning_checkpoint_version ||
            checkpoint.finality_depth == 0U ||
            checkpoint.reorganization_window == 0U)
            return ShieldedStoreError::unsafe_retention;
        const PruningPolicy policy{true, checkpoint.finality_depth,
                                   checkpoint.reorganization_window};
        if (checkpoint.genesis != expected_genesis_ ||
            checkpoint.active_tip != state_.tip() ||
            checkpoint.shielded_root != state_.root() ||
            checkpoint.tip_height != state_.height() ||
            checkpoint.prune_below_height !=
                pruning_detail::pruning_horizon(checkpoint.tip_height,
                                                 policy))
            return ShieldedStoreError::checkpoint_mismatch;
        auto next = state_;
        if (!next.set_undo_retention_limit(
                checkpoint.reorganization_window))
            return ShieldedStoreError::state_transition_failed;
        const auto result = persist(next);
        if (result != ShieldedStoreError::none) return result;
        state_ = std::move(next);
        return ShieldedStoreError::none;
    }

    const ShieldedState& state() const { return state_; }
};

} // namespace onuros
