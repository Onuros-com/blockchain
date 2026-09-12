#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>

namespace onuros {

struct PeerManagerLimits {
    std::size_t maximum_connections = 64U;
    std::size_t maximum_connections_per_address = 4U;
    std::uint64_t ban_seconds = 3600U;
    std::uint64_t handshake_timeout_seconds = 10U;
    std::uint64_t idle_timeout_seconds = 120U;
};

enum class PeerAdmissionError {
    none,
    banned,
    total_limit,
    address_limit,
    invalid_address
};

class PeerManager {
    PeerManagerLimits limits_;
    std::size_t connections_ = 0U;
    std::map<std::string, std::size_t> address_connections_;
    std::map<std::string, std::uint64_t> banned_until_;

public:
    explicit PeerManager(PeerManagerLimits limits) : limits_(limits) {}

    bool banned(const std::string& address, std::uint64_t now) const {
        const auto found = banned_until_.find(address);
        return found != banned_until_.end() && now < found->second;
    }

    PeerAdmissionError admit(const std::string& address, std::uint64_t now) {
        if (address.empty()) return PeerAdmissionError::invalid_address;
        if (banned(address, now)) return PeerAdmissionError::banned;
        if (connections_ >= limits_.maximum_connections)
            return PeerAdmissionError::total_limit;
        const auto found = address_connections_.find(address);
        const auto count = found == address_connections_.end() ? 0U : found->second;
        if (count >= limits_.maximum_connections_per_address)
            return PeerAdmissionError::address_limit;
        ++connections_;
        address_connections_[address] = count + 1U;
        return PeerAdmissionError::none;
    }

    void release(const std::string& address) {
        const auto found = address_connections_.find(address);
        if (found == address_connections_.end()) return;
        if (connections_ != 0U) --connections_;
        if (found->second <= 1U) address_connections_.erase(found);
        else --found->second;
    }

    void ban(const std::string& address, std::uint64_t now) {
        const auto until = limits_.ban_seconds >
                std::numeric_limits<std::uint64_t>::max() - now
            ? std::numeric_limits<std::uint64_t>::max()
            : now + limits_.ban_seconds;
        banned_until_[address] = std::max(banned_until_[address], until);
    }

    void prune_expired_bans(std::uint64_t now) {
        for (auto item = banned_until_.begin(); item != banned_until_.end();) {
            if (now >= item->second) item = banned_until_.erase(item);
            else ++item;
        }
    }

    std::size_t connection_count() const noexcept { return connections_; }
};

enum class PeerTimeout { none, handshake, idle };

class PeerDeadlineTracker {
    PeerManagerLimits limits_;
    std::uint64_t connected_at_;
    std::uint64_t last_activity_;
    bool handshake_complete_ = false;

public:
    PeerDeadlineTracker(PeerManagerLimits limits, std::uint64_t now)
        : limits_(limits), connected_at_(now), last_activity_(now) {}

    void mark_handshake_complete(std::uint64_t now) noexcept {
        handshake_complete_ = true;
        last_activity_ = now;
    }
    void mark_activity(std::uint64_t now) noexcept {
        last_activity_ = std::max(last_activity_, now);
    }
    PeerTimeout timeout(std::uint64_t now) const noexcept {
        if (!handshake_complete_ && now >= connected_at_ &&
            now - connected_at_ >= limits_.handshake_timeout_seconds)
            return PeerTimeout::handshake;
        if (handshake_complete_ && now >= last_activity_ &&
            now - last_activity_ >= limits_.idle_timeout_seconds)
            return PeerTimeout::idle;
        return PeerTimeout::none;
    }
};

} // namespace onuros
