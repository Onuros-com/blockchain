#pragma once

#include "onuros/persistent_shielded_state.hpp"
#include "onuros/pruning.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace onuros {

inline constexpr std::uint32_t state_snapshot_manifest_version = 1U;

struct StateSnapshotManifest {
    std::uint32_t version = state_snapshot_manifest_version;
    PruningCheckpoint checkpoint;
    Hash256 consensus_parameters_hash{};
    std::uint64_t content_size = 0U;
    Hash256 content_hash{};
};

enum class StateSnapshotError {
    none,
    malformed_manifest,
    unauthenticated_manifest,
    checkpoint_mismatch,
    consensus_parameters_mismatch,
    content_size_mismatch,
    content_hash_mismatch,
    invalid_shielded_state,
    io_error
};

namespace state_snapshot_detail {

inline constexpr std::array<std::uint8_t, 8> manifest_magic{
    'O', 'N', 'S', 'N', 'A', 'P', '0', '1'};

inline void append_u32(std::vector<std::uint8_t>& output,
                       std::uint32_t value) {
    detail::append_u32(output, value);
}

inline void append_u64(std::vector<std::uint8_t>& output,
                       std::uint64_t value) {
    detail::append_u64(output, value);
}

} // namespace state_snapshot_detail

inline StateSnapshotManifest make_state_snapshot_manifest(
        const std::vector<std::uint8_t>& content,
        const PruningCheckpoint& checkpoint,
        Hash256 consensus_parameters_hash) {
    static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t),
                  "snapshot size requires a 64-bit manifest field");
    StateSnapshotManifest manifest;
    manifest.checkpoint = checkpoint;
    manifest.consensus_parameters_hash = consensus_parameters_hash;
    manifest.content_size = static_cast<std::uint64_t>(content.size());
    manifest.content_hash = double_sha256(content);
    return manifest;
}

inline std::vector<std::uint8_t> encode_state_snapshot_manifest(
        const StateSnapshotManifest& manifest) {
    const auto checkpoint = encode_pruning_checkpoint(manifest.checkpoint);
    if (checkpoint.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("state snapshot checkpoint too large");
    std::vector<std::uint8_t> encoded(
        state_snapshot_detail::manifest_magic.begin(),
        state_snapshot_detail::manifest_magic.end());
    state_snapshot_detail::append_u32(encoded, manifest.version);
    state_snapshot_detail::append_u32(
        encoded, static_cast<std::uint32_t>(checkpoint.size()));
    encoded.insert(encoded.end(), checkpoint.begin(), checkpoint.end());
    detail::append_hash(encoded, manifest.consensus_parameters_hash);
    state_snapshot_detail::append_u64(encoded, manifest.content_size);
    detail::append_hash(encoded, manifest.content_hash);
    detail::append_hash(encoded, double_sha256(encoded));
    return encoded;
}

inline std::optional<StateSnapshotManifest> decode_state_snapshot_manifest(
        const std::vector<std::uint8_t>& encoded) {
    constexpr std::size_t fixed_size = 8U + 4U + 4U + 32U + 8U + 32U + 32U;
    if (encoded.size() < fixed_size ||
        !std::equal(state_snapshot_detail::manifest_magic.begin(),
                    state_snapshot_detail::manifest_magic.end(),
                    encoded.begin()))
        return std::nullopt;
    const auto checksum_offset = encoded.size() - Hash256{}.size();
    const auto checksum = double_sha256(std::vector<std::uint8_t>(
        encoded.begin(),
        encoded.begin() + static_cast<std::ptrdiff_t>(checksum_offset)));
    if (!std::equal(checksum.begin(), checksum.end(),
                    encoded.begin() +
                        static_cast<std::ptrdiff_t>(checksum_offset)))
        return std::nullopt;

    std::size_t offset = state_snapshot_detail::manifest_magic.size();
    StateSnapshotManifest manifest;
    std::uint32_t checkpoint_size = 0U;
    if (!detail::take_u32(encoded, offset, manifest.version) ||
        manifest.version != state_snapshot_manifest_version ||
        !detail::take_u32(encoded, offset, checkpoint_size) ||
        checkpoint_size != pruning_detail::checkpoint_encoded_size ||
        offset > checksum_offset || checkpoint_size > checksum_offset - offset)
        return std::nullopt;
    const std::vector<std::uint8_t> checkpoint_bytes(
        encoded.begin() + static_cast<std::ptrdiff_t>(offset),
        encoded.begin() + static_cast<std::ptrdiff_t>(offset + checkpoint_size));
    offset += checkpoint_size;
    const auto checkpoint = decode_pruning_checkpoint(checkpoint_bytes);
    if (!checkpoint) return std::nullopt;
    manifest.checkpoint = *checkpoint;
    const std::vector<std::uint8_t> payload(
        encoded.begin() + static_cast<std::ptrdiff_t>(offset),
        encoded.begin() + static_cast<std::ptrdiff_t>(checksum_offset));
    detail::ByteReader reader(payload);
    if (!reader.read_hash(manifest.consensus_parameters_hash) ||
        !reader.read_little_endian(manifest.content_size) ||
        !reader.read_hash(manifest.content_hash) || !reader.exhausted())
        return std::nullopt;
    return manifest;
}

inline Hash256 state_snapshot_manifest_id(
        const std::vector<std::uint8_t>& encoded_manifest) {
    return double_sha256(encoded_manifest);
}

inline StateSnapshotError import_authenticated_shielded_snapshot(
        const std::filesystem::path& destination,
        const std::vector<std::uint8_t>& encoded_manifest,
        const std::vector<std::uint8_t>& content,
        const Hash256& authenticated_manifest_id,
        const PruningCheckpoint& expected_checkpoint,
        const Hash256& expected_consensus_parameters_hash,
        std::size_t maximum_entries) {
    const auto manifest = decode_state_snapshot_manifest(encoded_manifest);
    if (!manifest) return StateSnapshotError::malformed_manifest;
    if (state_snapshot_manifest_id(encoded_manifest) !=
        authenticated_manifest_id)
        return StateSnapshotError::unauthenticated_manifest;
    if (encode_pruning_checkpoint(manifest->checkpoint) !=
        encode_pruning_checkpoint(expected_checkpoint))
        return StateSnapshotError::checkpoint_mismatch;
    if (manifest->consensus_parameters_hash !=
        expected_consensus_parameters_hash)
        return StateSnapshotError::consensus_parameters_mismatch;
    if (manifest->content_size != content.size())
        return StateSnapshotError::content_size_mismatch;
    if (manifest->content_hash != double_sha256(content))
        return StateSnapshotError::content_hash_mismatch;

    const auto snapshot =
        shielded_store_detail::decode(content, maximum_entries);
    if (!snapshot ||
        snapshot->genesis_block != manifest->checkpoint.genesis ||
        snapshot->tip_block != manifest->checkpoint.active_tip ||
        snapshot->current_root != manifest->checkpoint.shielded_root)
        return StateSnapshotError::invalid_shielded_state;
    if (!detail::create_atomic(destination, content))
        return StateSnapshotError::io_error;
    return StateSnapshotError::none;
}

} // namespace onuros
