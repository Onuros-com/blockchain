#pragma once

#include "onuros/block_format.hpp"
#include "onuros/chain_index.hpp"
#include "onuros/difficulty.hpp"
#include "onuros/pruning.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace onuros {

enum class BlockStoreError {
    none,
    io_error,
    database_too_large,
    corrupt_database,
    invalid_block_encoding,
    invalid_chain
};

struct StoredBlock {
    Block block;
    ChainWork work;
    bool body_retained = true;
};

enum class BlockBodyAvailability { retained, archive_required, unknown };

inline ChainWork chain_work_from_target_work(const Target256& work) {
    ChainWork result;
    for (std::size_t i = 0; i < result.limbs.size(); ++i) {
        result.limbs[i] = static_cast<std::uint64_t>(work.limbs[i * 2U]) |
            (static_cast<std::uint64_t>(work.limbs[i * 2U + 1U]) << 32U);
    }
    return result;
}

namespace detail {

inline void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    append_little_endian(output, value);
}

inline void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    append_little_endian(output, value);
}

inline bool take_u32(const std::vector<std::uint8_t>& input, std::size_t& offset,
                     std::uint32_t& value) {
    if (offset > input.size() || input.size() - offset < 4U) return false;
    value = 0U;
    for (std::size_t i = 0; i < 4U; ++i)
        value |= static_cast<std::uint32_t>(input[offset + i]) << (8U * i);
    offset += 4U;
    return true;
}

inline bool take_u64(const std::vector<std::uint8_t>& input, std::size_t& offset,
                     std::uint64_t& value) {
    if (offset > input.size() || input.size() - offset < 8U) return false;
    value = 0U;
    for (std::size_t i = 0; i < 8U; ++i)
        value |= static_cast<std::uint64_t>(input[offset + i]) << (8U * i);
    offset += 8U;
    return true;
}

inline bool sync_file(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const bool result = FlushFileBuffers(handle) != 0;
    CloseHandle(handle);
    return result;
#else
    const auto name = path.string();
    const int file = ::open(name.c_str(), O_RDONLY);
    if (file < 0) return false;
    const bool result = ::fsync(file) == 0;
    ::close(file);
    return result;
#endif
}

inline bool sync_parent_directory(const std::filesystem::path& path) {
#ifdef _WIN32
    (void)path;
    return true;
#else
    auto parent = path.parent_path();
    if (parent.empty()) parent = ".";
    const auto name = parent.string();
    const int directory = ::open(name.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory < 0) return false;
    const bool result = ::fsync(directory) == 0;
    ::close(directory);
    return result;
#endif
}

inline bool replace_file(const std::filesystem::path& temporary,
                         const std::filesystem::path& destination) {
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    const auto source = temporary.string();
    const auto target = destination.string();
    return ::rename(source.c_str(), target.c_str()) == 0;
#endif
}

inline bool create_atomic(const std::filesystem::path& destination,
                          const std::vector<std::uint8_t>& bytes) {
    auto temporary = destination;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) return false;
    }
    if (!sync_file(temporary) || !replace_file(temporary, destination)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    (void)sync_parent_directory(destination);
    return true;
}

inline bool append_synced(const std::filesystem::path& destination,
                          const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(destination, std::ios::binary | std::ios::app);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) return false;
    output.close();
    return sync_file(destination);
}

} // namespace detail

class PersistentBlockStore {
    static constexpr std::array<std::uint8_t, 8> magic_v2_ =
        {'O', 'N', 'U', 'R', 'D', 'B', '0', '2'};
    static constexpr std::array<std::uint8_t, 8> magic_v3_ =
        {'O', 'N', 'U', 'R', 'D', 'B', '0', '3'};
    static constexpr std::size_t minimum_v2_record_payload_ =
        4U + block_prefix_encoded_size + 32U;
    static constexpr std::size_t minimum_v3_record_payload_ =
        1U + 4U + block_header_encoded_size + 32U;

    std::filesystem::path path_;
    DecodeLimits decode_limits_;
    std::size_t max_database_bytes_;
    std::uintmax_t durable_size_ = 0U;
    std::vector<StoredBlock> blocks_;
    std::unordered_map<Hash256, std::size_t, Hash256Hasher> positions_;
    ChainIndex index_;
    bool version_three_ = false;

    static std::vector<std::uint8_t> serialize_record(
            const StoredBlock& stored, bool version_three) {
        const auto encoded = stored.body_retained
            ? encode_block(stored.block)
            : encode_block_header(stored.block.header);
        std::vector<std::uint8_t> payload;
        payload.reserve((version_three ? 1U : 0U) + 4U + encoded.size() + 32U);
        if (version_three)
            payload.push_back(stored.body_retained ? 1U : 0U);
        detail::append_u32(payload, static_cast<std::uint32_t>(encoded.size()));
        payload.insert(payload.end(), encoded.begin(), encoded.end());
        for (const auto limb : stored.work.limbs) detail::append_u64(payload, limb);

        std::vector<std::uint8_t> record;
        record.reserve(4U + payload.size() + 32U);
        detail::append_u32(record, static_cast<std::uint32_t>(payload.size()));
        record.insert(record.end(), payload.begin(), payload.end());
        const auto checksum = double_sha256(record);
        detail::append_hash(record, checksum);
        return record;
    }

    BlockStoreError parse(const std::vector<std::uint8_t>& bytes,
                          std::vector<StoredBlock>& parsed_blocks,
                          ChainIndex& parsed_index,
                          std::size_t& valid_size,
                          bool& version_three) const {
        if (bytes.size() < magic_v2_.size())
            return BlockStoreError::corrupt_database;
        if (std::equal(magic_v3_.begin(), magic_v3_.end(), bytes.begin()))
            version_three = true;
        else if (std::equal(magic_v2_.begin(), magic_v2_.end(), bytes.begin()))
            version_three = false;
        else
            return BlockStoreError::corrupt_database;
        std::size_t offset = magic_v2_.size();
        valid_size = offset;
        while (offset < bytes.size()) {
            const auto record_start = offset;
            std::uint32_t payload_size = 0U;
            if (!detail::take_u32(bytes, offset, payload_size)) break;
            const auto max_block_bytes =
                effective_block_limit(decode_limits_.max_block_bytes);
            const auto minimum_payload = version_three
                ? minimum_v3_record_payload_ : minimum_v2_record_payload_;
            const auto framing = version_three ? 37U : 36U;
            if (payload_size < minimum_payload ||
                payload_size > max_block_bytes + framing)
                return BlockStoreError::corrupt_database;
            if (offset > bytes.size() || payload_size > bytes.size() - offset ||
                bytes.size() - offset - payload_size < 32U)
                break;
            const auto payload_end = offset + payload_size;
            const std::vector<std::uint8_t> checksummed(
                bytes.begin() + static_cast<std::ptrdiff_t>(record_start),
                bytes.begin() + static_cast<std::ptrdiff_t>(payload_end));
            const auto checksum = double_sha256(checksummed);
            if (!std::equal(checksum.begin(), checksum.end(),
                            bytes.begin() + static_cast<std::ptrdiff_t>(payload_end)))
                return BlockStoreError::corrupt_database;

            bool body_retained = true;
            if (version_three) {
                if (offset == payload_end || bytes[offset] > 1U)
                    return BlockStoreError::corrupt_database;
                body_retained = bytes[offset++] == 1U;
            }
            std::uint32_t encoded_size = 0U;
            if (!detail::take_u32(bytes, offset, encoded_size) ||
                encoded_size > max_block_bytes ||
                encoded_size > payload_end - offset)
                return BlockStoreError::corrupt_database;
            std::vector<std::uint8_t> encoded(
                bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + encoded_size));
            offset += encoded_size;
            Block block;
            if (body_retained) {
                auto decoded = decode_block(encoded, decode_limits_);
                if (!decoded) return BlockStoreError::invalid_block_encoding;
                block = std::move(*decoded);
            } else {
                if (encoded.size() != block_header_encoded_size)
                    return BlockStoreError::invalid_block_encoding;
                detail::ByteReader reader(encoded);
                if (!detail::read_block_header(reader, block.header) ||
                    !reader.exhausted())
                    return BlockStoreError::invalid_block_encoding;
            }
            ChainWork work;
            for (auto& limb : work.limbs) {
                if (!detail::take_u64(bytes, offset, limb))
                    return BlockStoreError::corrupt_database;
            }
            if (offset != payload_end) return BlockStoreError::corrupt_database;
            offset += 32U;

            const auto target = decode_compact_target(block.header.compact_target);
            if (!target || !is_canonical_compact_target(block.header.compact_target) ||
                !(work == chain_work_from_target_work(work_for_target(*target))))
                return BlockStoreError::invalid_chain;
            const auto id = block_id(block.header);
            ChainIndexResult result;
            if (block.header.height == 0U) {
                if (!parsed_blocks.empty() || block.header.previous != Hash256{})
                    return BlockStoreError::invalid_chain;
                result = parsed_index.add_genesis(id, work, block.header.timestamp);
            } else {
                result = parsed_index.add_block(id, block.header.previous,
                    block.header.height, work, block.header.timestamp);
            }
            if (result.error != ChainIndexError::none)
                return BlockStoreError::invalid_chain;
            parsed_blocks.push_back({std::move(block), work, body_retained});
            valid_size = offset;
        }
        return BlockStoreError::none;
    }

    BlockStoreError replace_version_three(
            const std::vector<StoredBlock>& replacement) {
        std::vector<std::uint8_t> bytes(magic_v3_.begin(), magic_v3_.end());
        for (const auto& stored : replacement) {
            const auto record = serialize_record(stored, true);
            if (record.size() > max_database_bytes_ ||
                bytes.size() > max_database_bytes_ - record.size())
                return BlockStoreError::database_too_large;
            bytes.insert(bytes.end(), record.begin(), record.end());
        }
        std::vector<StoredBlock> verified_blocks;
        ChainIndex verified_index;
        std::size_t verified_size = 0U;
        bool verified_v3 = false;
        const auto verified = parse(bytes, verified_blocks, verified_index,
                                     verified_size, verified_v3);
        if (verified != BlockStoreError::none || !verified_v3 ||
            verified_size != bytes.size())
            return BlockStoreError::corrupt_database;
        if (!detail::create_atomic(path_, bytes)) return BlockStoreError::io_error;

        durable_size_ = bytes.size();
        blocks_ = std::move(verified_blocks);
        positions_.clear();
        positions_.reserve(blocks_.size());
        for (std::size_t i = 0U; i < blocks_.size(); ++i)
            positions_.emplace(block_id(blocks_[i].block.header), i);
        index_ = std::move(verified_index);
        version_three_ = true;
        return BlockStoreError::none;
    }

public:
    PersistentBlockStore(DecodeLimits decode_limits,
                         std::size_t max_database_bytes)
        : decode_limits_(decode_limits), max_database_bytes_(max_database_bytes) {}

    BlockStoreError open(const std::filesystem::path& path) {
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            if (error) return BlockStoreError::io_error;
            const std::vector<std::uint8_t> header(
                magic_v2_.begin(), magic_v2_.end());
            if (header.size() > max_database_bytes_)
                return BlockStoreError::database_too_large;
            if (!detail::create_atomic(path, header)) return BlockStoreError::io_error;
        }
        const auto size = std::filesystem::file_size(path, error);
        if (error) return BlockStoreError::io_error;
        if (size > max_database_bytes_ ||
            size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
            return BlockStoreError::database_too_large;
        std::ifstream input(path, std::ios::binary);
        if (!input) return BlockStoreError::io_error;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input && !bytes.empty()) return BlockStoreError::io_error;
        input.close();

        std::vector<StoredBlock> parsed_blocks;
        ChainIndex parsed_index;
        std::size_t valid_size = 0U;
        bool parsed_version_three = false;
        const auto result = parse(bytes, parsed_blocks, parsed_index, valid_size,
                                  parsed_version_three);
        if (result != BlockStoreError::none) return result;
        if (valid_size != bytes.size()) {
            std::filesystem::resize_file(path, valid_size, error);
            if (error || !detail::sync_file(path)) return BlockStoreError::io_error;
        }
        path_ = path;
        durable_size_ = valid_size;
        blocks_ = std::move(parsed_blocks);
        positions_.clear();
        positions_.reserve(blocks_.size());
        for (std::size_t i = 0; i < blocks_.size(); ++i)
            positions_.emplace(block_id(blocks_[i].block.header), i);
        index_ = std::move(parsed_index);
        version_three_ = parsed_version_three;
        return BlockStoreError::none;
    }

    BlockStoreError append(const Block& block, ChainWork work,
                           ChainIndexResult* index_result = nullptr) {
        if (path_.empty()) return BlockStoreError::io_error;
        if (blocks_.size() == std::numeric_limits<std::uint32_t>::max())
            return BlockStoreError::database_too_large;
        const auto encoded = encode_block(block);
        if (encoded.size() > effective_block_limit(decode_limits_.max_block_bytes) ||
            encoded.size() > std::numeric_limits<std::uint32_t>::max())
            return BlockStoreError::invalid_block_encoding;
        const auto target = decode_compact_target(block.header.compact_target);
        if (!target || !is_canonical_compact_target(block.header.compact_target) ||
            !(work == chain_work_from_target_work(work_for_target(*target))))
            return BlockStoreError::invalid_chain;

        const auto id = block_id(block.header);
        if (block.header.height == 0U && block.header.previous != Hash256{})
            return BlockStoreError::invalid_chain;
        const auto check = block.header.height == 0U
            ? index_.check_genesis(id, work)
            : index_.check_block(id, block.header.previous, block.header.height, work);
        if (check != ChainIndexError::none)
            return BlockStoreError::invalid_chain;
        const auto record = serialize_record({block, work, true}, version_three_);
        if (record.size() > max_database_bytes_ ||
            durable_size_ > max_database_bytes_ - record.size())
            return BlockStoreError::database_too_large;
        if (!detail::append_synced(path_, record)) return BlockStoreError::io_error;
        auto result = block.header.height == 0U
            ? index_.add_genesis(id, work, block.header.timestamp)
            : index_.add_block(id, block.header.previous, block.header.height,
                               work, block.header.timestamp);
        if (result.error != ChainIndexError::none)
            return BlockStoreError::invalid_chain;
        durable_size_ += record.size();
        blocks_.push_back({block, work, true});
        positions_.emplace(id, blocks_.size() - 1U);
        if (index_result != nullptr) *index_result = std::move(result);
        return BlockStoreError::none;
    }

    const StoredBlock* find(const Hash256& id) const {
        const auto position = positions_.find(id);
        return position == positions_.end() ? nullptr : &blocks_[position->second];
    }

    BlockBodyAvailability body_availability(const Hash256& id) const {
        const auto* stored = find(id);
        if (stored == nullptr) return BlockBodyAvailability::unknown;
        return stored->body_retained ? BlockBodyAvailability::retained
                                    : BlockBodyAvailability::archive_required;
    }

    BlockStoreError restore_body(const Block& block) {
        if (path_.empty()) return BlockStoreError::io_error;
        if (!has_valid_transaction_root(block))
            return BlockStoreError::invalid_block_encoding;
        const auto encoded = encode_block(block);
        if (encoded.size() >
                effective_block_limit(decode_limits_.max_block_bytes) ||
            !decode_block(encoded, decode_limits_))
            return BlockStoreError::invalid_block_encoding;
        const auto id = block_id(block.header);
        const auto position = positions_.find(id);
        if (position == positions_.end()) return BlockStoreError::invalid_chain;
        const auto& existing = blocks_[position->second];
        if (!(existing.block.header == block.header))
            return BlockStoreError::invalid_chain;
        if (existing.body_retained)
            return encode_block(existing.block) == encoded
                ? BlockStoreError::none
                : BlockStoreError::invalid_block_encoding;
        auto restored = blocks_;
        restored[position->second].block = block;
        restored[position->second].body_retained = true;
        return replace_version_three(restored);
    }

    BlockStoreError compact(const PruningPolicy& policy,
                            const PruningCheckpoint& checkpoint) {
        if (path_.empty() || blocks_.empty()) return BlockStoreError::io_error;
        const auto* active_tip = index_.active_tip();
        if (active_tip == nullptr) return BlockStoreError::invalid_chain;
        const auto genesis = block_id(blocks_.front().block.header);
        const auto tip = find(active_tip->id);
        if (tip == nullptr ||
            validate_pruning_checkpoint(checkpoint, policy, genesis,
                *active_tip, tip->block.header.shielded_root) !=
                    PruningCheckpointError::none)
            return BlockStoreError::invalid_chain;

        std::vector<StoredBlock> compacted = blocks_;
        for (auto& stored : compacted) {
            if (checkpoint.body_may_be_pruned(stored.block.header.height)) {
                stored.block.transactions.clear();
                stored.body_retained = false;
            }
        }
        return replace_version_three(compacted);
    }

    const std::vector<StoredBlock>& blocks() const { return blocks_; }
    const ChainIndex& index() const { return index_; }
};

} // namespace onuros
