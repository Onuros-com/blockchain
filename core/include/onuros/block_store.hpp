#pragma once

#include "onuros/block_format.hpp"
#include "onuros/chain_index.hpp"
#include "onuros/difficulty.hpp"

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
#define NOMINMAX
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
};

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
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ,
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
    static constexpr std::array<std::uint8_t, 8> magic_ =
        {'O', 'N', 'U', 'R', 'D', 'B', '0', '2'};
    static constexpr std::size_t minimum_record_payload_ = 4U + 132U + 32U;

    std::filesystem::path path_;
    DecodeLimits decode_limits_;
    std::size_t max_database_bytes_;
    std::uintmax_t durable_size_ = 0U;
    std::vector<StoredBlock> blocks_;
    std::unordered_map<Hash256, std::size_t, Hash256Hasher> positions_;
    ChainIndex index_;

    static std::vector<std::uint8_t> serialize_record(const StoredBlock& stored) {
        const auto encoded = encode_block(stored.block);
        std::vector<std::uint8_t> payload;
        payload.reserve(4U + encoded.size() + 32U);
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
                          std::size_t& valid_size) const {
        if (bytes.size() < magic_.size() ||
            !std::equal(magic_.begin(), magic_.end(), bytes.begin()))
            return BlockStoreError::corrupt_database;
        std::size_t offset = magic_.size();
        valid_size = offset;
        while (offset < bytes.size()) {
            const auto record_start = offset;
            std::uint32_t payload_size = 0U;
            if (!detail::take_u32(bytes, offset, payload_size)) break;
            if (payload_size < minimum_record_payload_ ||
                payload_size > decode_limits_.max_block_bytes + 36U)
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

            std::uint32_t encoded_size = 0U;
            if (!detail::take_u32(bytes, offset, encoded_size) ||
                encoded_size > decode_limits_.max_block_bytes ||
                encoded_size > payload_end - offset)
                return BlockStoreError::corrupt_database;
            std::vector<std::uint8_t> encoded(
                bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + encoded_size));
            offset += encoded_size;
            auto block = decode_block(encoded, decode_limits_);
            if (!block) return BlockStoreError::invalid_block_encoding;
            ChainWork work;
            for (auto& limb : work.limbs) {
                if (!detail::take_u64(bytes, offset, limb))
                    return BlockStoreError::corrupt_database;
            }
            if (offset != payload_end) return BlockStoreError::corrupt_database;
            offset += 32U;

            const auto target = decode_compact_target(block->header.compact_target);
            if (!target || !is_canonical_compact_target(block->header.compact_target) ||
                !(work == chain_work_from_target_work(work_for_target(*target))))
                return BlockStoreError::invalid_chain;
            const auto id = block_id(block->header);
            ChainIndexResult result;
            if (block->header.height == 0U) {
                if (!parsed_blocks.empty() || block->header.previous != Hash256{})
                    return BlockStoreError::invalid_chain;
                result = parsed_index.add_genesis(id, work, block->header.timestamp);
            } else {
                result = parsed_index.add_block(id, block->header.previous,
                    block->header.height, work, block->header.timestamp);
            }
            if (result.error != ChainIndexError::none)
                return BlockStoreError::invalid_chain;
            parsed_blocks.push_back({std::move(*block), work});
            valid_size = offset;
        }
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
            const std::vector<std::uint8_t> header(magic_.begin(), magic_.end());
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
        const auto result = parse(bytes, parsed_blocks, parsed_index, valid_size);
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
        return BlockStoreError::none;
    }

    BlockStoreError append(const Block& block, ChainWork work,
                           ChainIndexResult* index_result = nullptr) {
        if (path_.empty()) return BlockStoreError::io_error;
        if (blocks_.size() == std::numeric_limits<std::uint32_t>::max())
            return BlockStoreError::database_too_large;
        const auto encoded = encode_block(block);
        if (encoded.size() > decode_limits_.max_block_bytes ||
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
        const auto record = serialize_record({block, work});
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
        blocks_.push_back({block, work});
        positions_.emplace(id, blocks_.size() - 1U);
        if (index_result != nullptr) *index_result = std::move(result);
        return BlockStoreError::none;
    }

    const StoredBlock* find(const Hash256& id) const {
        const auto position = positions_.find(id);
        return position == positions_.end() ? nullptr : &blocks_[position->second];
    }

    const std::vector<StoredBlock>& blocks() const { return blocks_; }
    const ChainIndex& index() const { return index_; }
};

} // namespace onuros
