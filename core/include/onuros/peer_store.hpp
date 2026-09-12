#pragma once

#include "onuros/block_store.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

namespace onuros {

enum class PeerAddressFamily : std::uint8_t { ipv4 = 4U, ipv6 = 6U };

struct PeerAddress {
    PeerAddressFamily family = PeerAddressFamily::ipv4;
    std::array<std::uint8_t, 16> address{};
    std::uint16_t port = 0U;
};

inline bool operator<(const PeerAddress& left, const PeerAddress& right) {
    return std::tie(left.family, left.address, left.port) <
           std::tie(right.family, right.address, right.port);
}
inline bool operator==(const PeerAddress& left, const PeerAddress& right) {
    return left.family == right.family && left.address == right.address &&
           left.port == right.port;
}

struct PeerRecord {
    PeerAddress endpoint;
    std::uint64_t services = 0U;
    std::uint64_t last_success = 0U;
    std::uint64_t retry_after = 0U;
    std::uint32_t failures = 0U;
};

struct PeerStoreLimits {
    std::size_t maximum_records = 4'096U;
    std::uint64_t base_retry_seconds = 30U;
    std::uint64_t maximum_retry_seconds = 3'600U;
};

class PersistentPeerStore {
    static constexpr std::array<std::uint8_t, 8> magic_ =
        {'O', 'N', 'U', 'R', 'P', 'E', 'E', 'R'};
    static constexpr std::size_t record_size_ = 47U;
    PeerStoreLimits limits_;
    std::map<PeerAddress, PeerRecord> records_;

    static bool valid(const PeerAddress& endpoint) {
        if (endpoint.port == 0U) return false;
        if (endpoint.family != PeerAddressFamily::ipv4 &&
            endpoint.family != PeerAddressFamily::ipv6)
            return false;
        if (endpoint.family == PeerAddressFamily::ipv4) {
            for (std::size_t i = 4U; i < endpoint.address.size(); ++i)
                if (endpoint.address[i] != 0U) return false;
        }
        return std::any_of(endpoint.address.begin(), endpoint.address.end(),
                           [](std::uint8_t byte) { return byte != 0U; });
    }

    std::vector<std::uint8_t> encode() const {
        std::vector<std::uint8_t> output(magic_.begin(), magic_.end());
        detail::append_little_endian(output,
            static_cast<std::uint32_t>(records_.size()));
        for (const auto& item : records_) {
            const auto& record = item.second;
            output.push_back(static_cast<std::uint8_t>(record.endpoint.family));
            output.insert(output.end(), record.endpoint.address.begin(),
                          record.endpoint.address.end());
            detail::append_little_endian(output, record.endpoint.port);
            detail::append_little_endian(output, record.services);
            detail::append_little_endian(output, record.last_success);
            detail::append_little_endian(output, record.retry_after);
            detail::append_little_endian(output, record.failures);
        }
        detail::append_hash(output, double_sha256(output));
        return output;
    }

public:
    explicit PersistentPeerStore(PeerStoreLimits limits) : limits_(limits) {}

    bool remember(const PeerAddress& endpoint, std::uint64_t services) {
        if (!valid(endpoint)) return false;
        const auto found = records_.find(endpoint);
        if (found != records_.end()) {
            found->second.services |= services;
            return true;
        }
        if (records_.size() >= limits_.maximum_records) return false;
        records_.emplace(endpoint, PeerRecord{endpoint, services, 0U, 0U, 0U});
        return true;
    }

    bool mark_success(const PeerAddress& endpoint, std::uint64_t now,
                      std::uint64_t services) {
        if (!remember(endpoint, services)) return false;
        auto& record = records_.at(endpoint);
        record.last_success = std::max(record.last_success, now);
        record.retry_after = 0U;
        record.failures = 0U;
        return true;
    }

    bool mark_failure(const PeerAddress& endpoint, std::uint64_t now) {
        const auto found = records_.find(endpoint);
        if (found == records_.end()) return false;
        auto& record = found->second;
        if (record.failures != std::numeric_limits<std::uint32_t>::max())
            ++record.failures;
        std::uint64_t delay = limits_.base_retry_seconds;
        for (std::uint32_t i = 1U; i < record.failures &&
                delay < limits_.maximum_retry_seconds; ++i) {
            delay = delay > limits_.maximum_retry_seconds / 2U
                ? limits_.maximum_retry_seconds : delay * 2U;
        }
        delay = std::min(delay, limits_.maximum_retry_seconds);
        record.retry_after = delay > std::numeric_limits<std::uint64_t>::max() - now
            ? std::numeric_limits<std::uint64_t>::max() : now + delay;
        return true;
    }

    std::vector<PeerRecord> candidates(std::uint64_t now,
                                       std::size_t maximum) const {
        std::vector<PeerRecord> result;
        for (const auto& item : records_) {
            if (item.second.retry_after <= now) result.push_back(item.second);
        }
        std::sort(result.begin(), result.end(), [](const PeerRecord& left,
                                                   const PeerRecord& right) {
            if (left.failures != right.failures)
                return left.failures < right.failures;
            if (left.last_success != right.last_success)
                return left.last_success > right.last_success;
            return left.endpoint < right.endpoint;
        });
        if (result.size() > maximum) result.resize(maximum);
        return result;
    }

    bool save(const std::filesystem::path& path) const {
        return detail::create_atomic(path, encode());
    }

    bool load(const std::filesystem::path& path) {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        const auto maximum = magic_.size() + sizeof(std::uint32_t) + 32U +
            limits_.maximum_records * record_size_;
        if (error || size < magic_.size() + sizeof(std::uint32_t) + 32U ||
            size > maximum)
            return false;
        std::ifstream input(path, std::ios::binary);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input) return false;
        if (!std::equal(magic_.begin(), magic_.end(), bytes.begin())) return false;
        const std::vector<std::uint8_t> payload(bytes.begin(), bytes.end() - 32);
        Hash256 checksum{};
        std::copy(bytes.end() - 32, bytes.end(), checksum.begin());
        if (double_sha256(payload) != checksum) return false;
        detail::ByteReader reader(payload);
        std::vector<std::uint8_t> ignored;
        std::uint32_t count = 0U;
        if (!reader.read_bytes(magic_.size(), ignored) ||
            !reader.read_little_endian(count) || count > limits_.maximum_records ||
            reader.remaining() != static_cast<std::size_t>(count) * record_size_)
            return false;
        std::map<PeerAddress, PeerRecord> candidate;
        for (std::uint32_t i = 0U; i < count; ++i) {
            std::uint8_t family = 0U;
            PeerRecord record;
            std::vector<std::uint8_t> address;
            if (!reader.read_little_endian(family) ||
                !reader.read_bytes(record.endpoint.address.size(), address) ||
                !reader.read_little_endian(record.endpoint.port) ||
                !reader.read_little_endian(record.services) ||
                !reader.read_little_endian(record.last_success) ||
                !reader.read_little_endian(record.retry_after) ||
                !reader.read_little_endian(record.failures))
                return false;
            record.endpoint.family = static_cast<PeerAddressFamily>(family);
            std::copy(address.begin(), address.end(), record.endpoint.address.begin());
            if (!valid(record.endpoint) ||
                !candidate.emplace(record.endpoint, record).second)
                return false;
        }
        records_ = std::move(candidate);
        return true;
    }

    std::size_t size() const noexcept { return records_.size(); }
};

} // namespace onuros
