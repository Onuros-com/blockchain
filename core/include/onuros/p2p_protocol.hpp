#pragma once

#include "onuros/block_format.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <set>
#include <vector>

namespace onuros {

// Encodes as the ASCII bytes "ONUR" on the wire.
inline constexpr std::uint32_t stage7_network_magic = 0x52554e4fU;
inline constexpr std::uint16_t stage7_protocol_version = 1U;
inline constexpr std::size_t p2p_frame_header_size = 52U;

enum class P2pMessageType : std::uint16_t {
    hello = 1U,
    hello_ack = 2U,
    ping = 3U,
    pong = 4U,
    transaction_inventory = 10U,
    get_transactions = 11U,
    transactions = 12U,
    compact_block = 20U,
    get_block_transactions = 21U,
    block_transactions = 22U,
    block_inventory = 23U,
    get_archive_block = 24U,
    archive_block = 25U,
    get_headers = 30U,
    headers = 31U,
    mining_job = 40U,
    mining_solution = 41U,
    mining_result = 42U,
    disconnect = 255U
};

inline bool known_message_type(std::uint16_t value) noexcept {
    switch (static_cast<P2pMessageType>(value)) {
        case P2pMessageType::hello:
        case P2pMessageType::hello_ack:
        case P2pMessageType::ping:
        case P2pMessageType::pong:
        case P2pMessageType::transaction_inventory:
        case P2pMessageType::get_transactions:
        case P2pMessageType::transactions:
        case P2pMessageType::compact_block:
        case P2pMessageType::get_block_transactions:
        case P2pMessageType::block_transactions:
        case P2pMessageType::block_inventory:
        case P2pMessageType::get_archive_block:
        case P2pMessageType::archive_block:
        case P2pMessageType::get_headers:
        case P2pMessageType::headers:
        case P2pMessageType::mining_job:
        case P2pMessageType::mining_solution:
        case P2pMessageType::mining_result:
        case P2pMessageType::disconnect:
            return true;
    }
    return false;
}

struct P2pFrame {
    std::uint16_t protocol_version = stage7_protocol_version;
    P2pMessageType type = P2pMessageType::disconnect;
    std::uint64_t request_id = 0U;
    std::vector<std::uint8_t> payload;
};

struct P2pFrameLimits {
    std::uint32_t network_magic = stage7_network_magic;
    std::uint16_t minimum_protocol = stage7_protocol_version;
    std::uint16_t maximum_protocol = stage7_protocol_version;
    std::size_t maximum_payload_bytes = 256U * 1024U;
};

enum class P2pFrameError {
    none,
    truncated,
    wrong_network,
    incompatible_protocol,
    unknown_message,
    oversized_payload,
    length_mismatch,
    checksum_mismatch
};

struct P2pFrameDecodeResult {
    P2pFrameError error = P2pFrameError::none;
    P2pFrame frame;
};

inline std::vector<std::uint8_t> encode_p2p_frame(
        const P2pFrame& frame,
        std::uint32_t network_magic = stage7_network_magic) {
    if (frame.payload.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("P2P payload exceeds canonical length field");
    std::vector<std::uint8_t> output;
    output.reserve(p2p_frame_header_size + frame.payload.size());
    detail::append_little_endian(output, network_magic);
    detail::append_little_endian(output, frame.protocol_version);
    detail::append_little_endian(output,
        static_cast<std::uint16_t>(frame.type));
    detail::append_little_endian(output, frame.request_id);
    detail::append_little_endian(output,
        static_cast<std::uint32_t>(frame.payload.size()));
    detail::append_hash(output, double_sha256(frame.payload));
    output.insert(output.end(), frame.payload.begin(), frame.payload.end());
    return output;
}

inline P2pFrameDecodeResult decode_p2p_frame(
        const std::vector<std::uint8_t>& input, const P2pFrameLimits& limits) {
    if (input.size() < p2p_frame_header_size)
        return {P2pFrameError::truncated, {}};
    detail::ByteReader reader(input);
    std::uint32_t magic = 0U;
    std::uint16_t protocol = 0U;
    std::uint16_t raw_type = 0U;
    std::uint64_t request_id = 0U;
    std::uint32_t payload_size = 0U;
    Hash256 checksum{};
    if (!reader.read_little_endian(magic) ||
        !reader.read_little_endian(protocol) ||
        !reader.read_little_endian(raw_type) ||
        !reader.read_little_endian(request_id) ||
        !reader.read_little_endian(payload_size) ||
        !reader.read_hash(checksum))
        return {P2pFrameError::truncated, {}};
    if (magic != limits.network_magic) return {P2pFrameError::wrong_network, {}};
    if (protocol < limits.minimum_protocol ||
        protocol > limits.maximum_protocol)
        return {P2pFrameError::incompatible_protocol, {}};
    if (!known_message_type(raw_type)) return {P2pFrameError::unknown_message, {}};
    if (payload_size > limits.maximum_payload_bytes)
        return {P2pFrameError::oversized_payload, {}};
    if (reader.remaining() != payload_size)
        return {P2pFrameError::length_mismatch, {}};
    P2pFrame frame;
    frame.protocol_version = protocol;
    frame.type = static_cast<P2pMessageType>(raw_type);
    frame.request_id = request_id;
    if (!reader.read_bytes(payload_size, frame.payload))
        return {P2pFrameError::truncated, {}};
    if (double_sha256(frame.payload) != checksum)
        return {P2pFrameError::checksum_mismatch, {}};
    return {P2pFrameError::none, std::move(frame)};
}

inline constexpr std::uint64_t p2p_service_full_node = 1ULL << 0U;
inline constexpr std::uint64_t p2p_service_compact_relay = 1ULL << 1U;
inline constexpr std::uint64_t p2p_service_pruned_node = 1ULL << 2U;
inline constexpr std::uint64_t p2p_service_authenticated_transport = 1ULL << 3U;
inline constexpr std::uint64_t p2p_service_archive_node = 1ULL << 4U;

struct HelloMessage {
    Hash256 chain_id{};
    Hash256 genesis_hash{};
    std::uint16_t minimum_protocol = stage7_protocol_version;
    std::uint16_t maximum_protocol = stage7_protocol_version;
    std::uint64_t services = 0U;
    std::uint64_t node_nonce = 0U;
    Height best_height = 0U;
    Hash256 cumulative_work{};
};

inline constexpr std::size_t hello_message_encoded_size = 124U;

inline std::vector<std::uint8_t> encode_hello(const HelloMessage& hello) {
    std::vector<std::uint8_t> output;
    output.reserve(hello_message_encoded_size);
    detail::append_hash(output, hello.chain_id);
    detail::append_hash(output, hello.genesis_hash);
    detail::append_little_endian(output, hello.minimum_protocol);
    detail::append_little_endian(output, hello.maximum_protocol);
    detail::append_little_endian(output, hello.services);
    detail::append_little_endian(output, hello.node_nonce);
    detail::append_little_endian(output, hello.best_height);
    detail::append_hash(output, hello.cumulative_work);
    return output;
}

inline std::optional<HelloMessage> decode_hello(
        const std::vector<std::uint8_t>& input) {
    if (input.size() != hello_message_encoded_size) return std::nullopt;
    detail::ByteReader reader(input);
    HelloMessage hello;
    if (!reader.read_hash(hello.chain_id) ||
        !reader.read_hash(hello.genesis_hash) ||
        !reader.read_little_endian(hello.minimum_protocol) ||
        !reader.read_little_endian(hello.maximum_protocol) ||
        !reader.read_little_endian(hello.services) ||
        !reader.read_little_endian(hello.node_nonce) ||
        !reader.read_little_endian(hello.best_height) ||
        !reader.read_hash(hello.cumulative_work) || !reader.exhausted())
        return std::nullopt;
    return hello;
}

struct HandshakePolicy {
    Hash256 chain_id{};
    Hash256 genesis_hash{};
    std::uint16_t minimum_protocol = stage7_protocol_version;
    std::uint16_t maximum_protocol = stage7_protocol_version;
    std::uint64_t local_nonce = 0U;
    std::uint64_t required_services = 0U;
    bool require_authenticated_transport = false;
};

enum class HandshakeError {
    none,
    wrong_chain,
    wrong_genesis,
    self_connection,
    incompatible_protocol,
    missing_service,
    unauthenticated_transport
};

struct HandshakeResult {
    HandshakeError error = HandshakeError::none;
    std::uint16_t negotiated_protocol = 0U;
};

inline HandshakeResult validate_hello(const HelloMessage& remote,
                                      const HandshakePolicy& policy,
                                      bool transport_authenticated) noexcept {
    if (remote.chain_id != policy.chain_id) return {HandshakeError::wrong_chain};
    if (remote.genesis_hash != policy.genesis_hash)
        return {HandshakeError::wrong_genesis};
    if (remote.node_nonce == policy.local_nonce)
        return {HandshakeError::self_connection};
    const auto minimum = std::max(remote.minimum_protocol,
                                  policy.minimum_protocol);
    const auto maximum = std::min(remote.maximum_protocol,
                                  policy.maximum_protocol);
    if (minimum > maximum) return {HandshakeError::incompatible_protocol};
    if ((remote.services & policy.required_services) != policy.required_services)
        return {HandshakeError::missing_service};
    if (policy.require_authenticated_transport && !transport_authenticated)
        return {HandshakeError::unauthenticated_transport};
    return {HandshakeError::none, maximum};
}

struct InventoryLimits {
    std::size_t maximum_items = 4096U;
    std::size_t maximum_seen_items = 65'536U;
};

inline std::vector<std::uint8_t> encode_inventory(
        const std::vector<Hash256>& identifiers) {
    if (identifiers.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("inventory count exceeds canonical field");
    std::vector<std::uint8_t> output;
    output.reserve(4U + identifiers.size() * 32U);
    detail::append_little_endian(output,
        static_cast<std::uint32_t>(identifiers.size()));
    for (const auto& identifier : identifiers)
        detail::append_hash(output, identifier);
    return output;
}

inline std::optional<std::vector<Hash256>> decode_inventory(
        const std::vector<std::uint8_t>& input, const InventoryLimits& limits) {
    detail::ByteReader reader(input);
    std::uint32_t count = 0U;
    if (!reader.read_little_endian(count) || count > limits.maximum_items ||
        reader.remaining() != static_cast<std::size_t>(count) * 32U)
        return std::nullopt;
    std::vector<Hash256> identifiers;
    identifiers.reserve(count);
    for (std::uint32_t i = 0U; i < count; ++i) {
        Hash256 identifier{};
        if (!reader.read_hash(identifier)) return std::nullopt;
        identifiers.push_back(identifier);
    }
    return identifiers;
}

class SeenInventory {
    std::size_t maximum_items_;
    std::deque<Hash256> order_;
    std::set<Hash256> identifiers_;

public:
    explicit SeenInventory(std::size_t maximum_items)
        : maximum_items_(maximum_items) {}

    bool remember(const Hash256& identifier) {
        if (maximum_items_ == 0U || identifiers_.find(identifier) != identifiers_.end())
            return false;
        while (order_.size() >= maximum_items_) {
            identifiers_.erase(order_.front());
            order_.pop_front();
        }
        order_.push_back(identifier);
        identifiers_.insert(identifier);
        return true;
    }

    bool contains(const Hash256& identifier) const {
        return identifiers_.find(identifier) != identifiers_.end();
    }

    std::size_t size() const noexcept { return identifiers_.size(); }
};

} // namespace onuros
