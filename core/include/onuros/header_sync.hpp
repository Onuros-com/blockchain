#pragma once

#include "onuros/block_format.hpp"
#include "onuros/block_store.hpp"
#include "onuros/chain_index.hpp"
#include "onuros/difficulty.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace onuros {

struct HeaderRequest {
    std::vector<Hash256> locator;
    Hash256 stop{};
};

inline std::vector<std::uint8_t> encode_header_request(
        const HeaderRequest& request) {
    if (request.locator.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("header locator is too large");
    std::vector<std::uint8_t> output;
    output.reserve(36U + request.locator.size() * 32U);
    detail::append_little_endian(output,
        static_cast<std::uint32_t>(request.locator.size()));
    for (const auto& identifier : request.locator)
        detail::append_hash(output, identifier);
    detail::append_hash(output, request.stop);
    return output;
}

inline std::optional<HeaderRequest> decode_header_request(
        const std::vector<std::uint8_t>& input, std::uint32_t maximum_locators) {
    detail::ByteReader reader(input);
    HeaderRequest request;
    std::uint32_t count = 0U;
    if (!reader.read_little_endian(count) || count == 0U ||
        count > maximum_locators ||
        reader.remaining() != static_cast<std::size_t>(count) * 32U + 32U)
        return std::nullopt;
    request.locator.reserve(count);
    for (std::uint32_t i = 0U; i < count; ++i) {
        Hash256 identifier{};
        if (!reader.read_hash(identifier)) return std::nullopt;
        request.locator.push_back(identifier);
    }
    if (!reader.read_hash(request.stop) || !reader.exhausted())
        return std::nullopt;
    return request;
}

inline std::vector<std::uint8_t> encode_headers(
        const std::vector<BlockHeader>& headers) {
    if (headers.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("header batch is too large");
    std::vector<std::uint8_t> output;
    output.reserve(4U + headers.size() * block_header_encoded_size);
    detail::append_little_endian(output,
        static_cast<std::uint32_t>(headers.size()));
    for (const auto& header : headers) {
        const auto encoded = encode_block_header(header);
        output.insert(output.end(), encoded.begin(), encoded.end());
    }
    return output;
}

inline std::optional<std::vector<BlockHeader>> decode_headers(
        const std::vector<std::uint8_t>& input, std::uint32_t maximum_headers) {
    detail::ByteReader reader(input);
    std::uint32_t count = 0U;
    if (!reader.read_little_endian(count) || count > maximum_headers ||
        reader.remaining() !=
            static_cast<std::size_t>(count) * block_header_encoded_size)
        return std::nullopt;
    std::vector<BlockHeader> headers;
    headers.reserve(count);
    for (std::uint32_t i = 0U; i < count; ++i) {
        BlockHeader header;
        if (!detail::read_block_header(reader, header)) return std::nullopt;
        headers.push_back(std::move(header));
    }
    return reader.exhausted()
        ? std::optional<std::vector<BlockHeader>>{std::move(headers)}
        : std::nullopt;
}

struct HeaderSyncLimits {
    std::uint32_t block_version = 1U;
    // 1,600 headers plus the count fit below the default 256 KiB P2P frame.
    std::uint32_t maximum_headers_per_batch = 1'600U;
    std::uint64_t maximum_future_seconds = 120U;
    DifficultyParameters difficulty;
};

struct HeaderSyncEntry {
    BlockHeader header;
    Hash256 id{};
    ChainWork block_work{};
    ChainWork accumulated_work{};
};

enum class HeaderSyncError {
    none,
    empty_batch,
    oversized_batch,
    unknown_parent,
    duplicate_header,
    unsupported_version,
    invalid_height,
    invalid_timestamp,
    unexpected_target,
    invalid_proof_of_work,
    invalid_work,
    accumulated_work_overflow
};

struct HeaderSyncResult {
    HeaderSyncError error = HeaderSyncError::none;
    std::size_t accepted = 0U;
    bool stronger_tip = false;
};

class HeaderSyncChain {
    HeaderSyncLimits limits_;
    std::function<Hash256(const BlockHeader&)> pow_hash_;
    std::unordered_map<Hash256, HeaderSyncEntry, Hash256Hasher> entries_;
    std::optional<Hash256> best_tip_;

    std::optional<std::uint64_t> median_time(const HeaderSyncEntry& parent) const {
        std::vector<std::uint64_t> timestamps;
        timestamps.reserve(11U);
        const HeaderSyncEntry* cursor = &parent;
        for (std::size_t i = 0U; i < 11U; ++i) {
            timestamps.push_back(cursor->header.timestamp);
            if (cursor->header.height == 0U) break;
            const auto found = entries_.find(cursor->header.previous);
            if (found == entries_.end()) return std::nullopt;
            cursor = &found->second;
        }
        return median_time_past(timestamps);
    }

    std::optional<std::uint32_t> expected_target(
            const HeaderSyncEntry& parent, Height next_height) const {
        const auto interval = limits_.difficulty.retarget_interval;
        if (interval < 2U) return std::nullopt;
        std::uint64_t first_timestamp = parent.header.timestamp;
        if (next_height % interval == 0U) {
            const HeaderSyncEntry* cursor = &parent;
            for (std::uint32_t i = 1U; i < interval; ++i) {
                const auto found = entries_.find(cursor->header.previous);
                if (found == entries_.end()) return std::nullopt;
                cursor = &found->second;
            }
            first_timestamp = cursor->header.timestamp;
        }
        return next_compact_target(parent.header.compact_target, next_height,
            first_timestamp, parent.header.timestamp, limits_.difficulty);
    }

public:
    HeaderSyncChain(HeaderSyncLimits limits,
                    std::function<Hash256(const BlockHeader&)> pow_hash)
        : limits_(std::move(limits)), pow_hash_(std::move(pow_hash)) {}

    HeaderSyncError seed(const BlockHeader& header, ChainWork accumulated_work) {
        // Seeds are trusted only after the corresponding full block has passed
        // ordinary consensus validation. Network headers enter through accept().
        const auto identifier = block_id(header);
        if (entries_.find(identifier) != entries_.end())
            return HeaderSyncError::duplicate_header;
        if (is_zero(accumulated_work)) return HeaderSyncError::invalid_work;
        const auto target_work = work_for_compact_target(
            header.compact_target, limits_.difficulty.proof_of_work_limit);
        if (!target_work || is_zero(*target_work)) return HeaderSyncError::invalid_work;
        entries_.emplace(identifier, HeaderSyncEntry{header, identifier,
            chain_work_from_target_work(*target_work), accumulated_work});
        if (!best_tip_ || entries_.at(*best_tip_).accumulated_work < accumulated_work)
            best_tip_ = identifier;
        return HeaderSyncError::none;
    }

    HeaderSyncResult accept(const std::vector<BlockHeader>& headers,
                            std::uint64_t adjusted_time) {
        if (headers.empty()) return {HeaderSyncError::empty_batch};
        if (headers.size() > limits_.maximum_headers_per_batch)
            return {HeaderSyncError::oversized_batch};
        HeaderSyncChain candidate = *this;
        for (const auto& header : headers) {
            const auto identifier = block_id(header);
            if (candidate.entries_.find(identifier) != candidate.entries_.end())
                return {HeaderSyncError::duplicate_header};
            const auto parent = candidate.entries_.find(header.previous);
            if (parent == candidate.entries_.end())
                return {HeaderSyncError::unknown_parent};
            if (header.version != limits_.block_version)
                return {HeaderSyncError::unsupported_version};
            if (parent->second.header.height == std::numeric_limits<Height>::max() ||
                header.height != parent->second.header.height + 1U)
                return {HeaderSyncError::invalid_height};
            const auto median = candidate.median_time(parent->second);
            if (!median || header.timestamp <= *median ||
                (header.timestamp > adjusted_time &&
                 header.timestamp - adjusted_time > limits_.maximum_future_seconds))
                return {HeaderSyncError::invalid_timestamp};
            const auto target = candidate.expected_target(parent->second, header.height);
            if (!target || header.compact_target != *target)
                return {HeaderSyncError::unexpected_target};
            if (!pow_hash_ || !hash_meets_compact_target(pow_hash_(header),
                    header.compact_target, limits_.difficulty.proof_of_work_limit))
                return {HeaderSyncError::invalid_proof_of_work};
            const auto target_work = work_for_compact_target(
                header.compact_target, limits_.difficulty.proof_of_work_limit);
            if (!target_work || is_zero(*target_work))
                return {HeaderSyncError::invalid_work};
            const auto work = chain_work_from_target_work(*target_work);
            const auto accumulated = add_chain_work(
                parent->second.accumulated_work, work);
            if (!accumulated) return {HeaderSyncError::accumulated_work_overflow};
            candidate.entries_.emplace(identifier, HeaderSyncEntry{
                header, identifier, work, *accumulated});
            if (!candidate.best_tip_ ||
                candidate.entries_.at(*candidate.best_tip_).accumulated_work <
                    *accumulated)
                candidate.best_tip_ = identifier;
        }
        const auto previous_tip = best_tip_;
        const bool stronger = candidate.best_tip_ != previous_tip;
        *this = std::move(candidate);
        return {HeaderSyncError::none, headers.size(), stronger};
    }

    const HeaderSyncEntry* find(const Hash256& identifier) const {
        const auto found = entries_.find(identifier);
        return found == entries_.end() ? nullptr : &found->second;
    }
    const HeaderSyncEntry* best_tip() const {
        return best_tip_ ? find(*best_tip_) : nullptr;
    }
    std::size_t size() const noexcept { return entries_.size(); }
};

} // namespace onuros
