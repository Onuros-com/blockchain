#include "onuros/p2p_protocol.hpp"

#include <iostream>
#include <stdexcept>

using namespace onuros;

namespace {
unsigned checks = 0U;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}
Hash256 value(std::uint8_t byte) { Hash256 hash{}; hash.back() = byte; return hash; }
}

int main() {
    try {
        P2pFrame frame;
        frame.type = P2pMessageType::ping;
        frame.request_id = 42U;
        frame.payload = {1U, 2U, 3U};
        const P2pFrameLimits limits{};
        const auto encoded = encode_p2p_frame(frame);
        check(encoded.size() == p2p_frame_header_size + frame.payload.size(),
              "frame has fixed bounded header");
        auto decoded = decode_p2p_frame(encoded, limits);
        check(decoded.error == P2pFrameError::none &&
              decoded.frame.request_id == 42U && decoded.frame.payload == frame.payload,
              "frame canonical round trip");

        auto malformed = encoded;
        malformed[0] ^= 1U;
        check(decode_p2p_frame(malformed, limits).error == P2pFrameError::wrong_network,
              "wrong network rejected");
        malformed = encoded;
        malformed[4] = 2U;
        check(decode_p2p_frame(malformed, limits).error ==
                  P2pFrameError::incompatible_protocol,
              "unsupported frame protocol rejected");
        malformed = encoded;
        malformed[6] = 99U;
        check(decode_p2p_frame(malformed, limits).error ==
                  P2pFrameError::unknown_message,
              "unknown message rejected");
        malformed = encoded;
        malformed.back() ^= 1U;
        check(decode_p2p_frame(malformed, limits).error ==
                  P2pFrameError::checksum_mismatch,
              "payload corruption rejected");
        malformed = encoded;
        malformed.pop_back();
        check(decode_p2p_frame(malformed, limits).error ==
                  P2pFrameError::length_mismatch,
              "truncated payload rejected before parsing");
        check(decode_p2p_frame(encoded, {stage7_network_magic, 1U, 1U, 2U}).error ==
                  P2pFrameError::oversized_payload,
              "payload limit enforced before message parsing");

        HelloMessage hello;
        hello.chain_id = value(1U);
        hello.genesis_hash = value(2U);
        hello.services = p2p_service_full_node | p2p_service_compact_relay |
                         p2p_service_authenticated_transport;
        hello.node_nonce = 99U;
        hello.best_height = 123U;
        hello.cumulative_work = value(3U);
        const auto encoded_hello = encode_hello(hello);
        const auto decoded_hello = decode_hello(encoded_hello);
        check(decoded_hello && decoded_hello->chain_id == hello.chain_id &&
              decoded_hello->services == hello.services,
              "hello canonical round trip");

        HandshakePolicy policy;
        policy.chain_id = hello.chain_id;
        policy.genesis_hash = hello.genesis_hash;
        policy.local_nonce = 100U;
        policy.required_services = p2p_service_compact_relay;
        policy.require_authenticated_transport = true;
        check(validate_hello(hello, policy, true).error == HandshakeError::none,
              "compatible authenticated peer accepted");
        check(validate_hello(hello, policy, false).error ==
                  HandshakeError::unauthenticated_transport,
              "public-mode policy rejects unauthenticated transport");
        auto changed = hello;
        changed.chain_id = value(9U);
        check(validate_hello(changed, policy, true).error == HandshakeError::wrong_chain,
              "wrong chain rejected");
        changed = hello;
        changed.node_nonce = policy.local_nonce;
        check(validate_hello(changed, policy, true).error ==
                  HandshakeError::self_connection,
              "self connection rejected");
        changed = hello;
        changed.minimum_protocol = 2U;
        changed.maximum_protocol = 3U;
        check(validate_hello(changed, policy, true).error ==
                  HandshakeError::incompatible_protocol,
              "incompatible handshake protocol rejected");

        const std::vector<Hash256> inventory{value(10U), value(11U)};
        const auto encoded_inventory = encode_inventory(inventory);
        const auto decoded_inventory = decode_inventory(encoded_inventory, {4U, 8U});
        check(decoded_inventory && *decoded_inventory == inventory,
              "inventory canonical round trip");
        check(!decode_inventory(encoded_inventory, {1U, 8U}),
              "inventory item limit enforced");
        malformed = encoded_inventory;
        malformed.push_back(0U);
        check(!decode_inventory(malformed, {4U, 8U}),
              "inventory trailing data rejected");

        SeenInventory seen(2U);
        check(seen.remember(value(1U)) && !seen.remember(value(1U)),
              "duplicate inventory suppressed");
        check(seen.remember(value(2U)) && seen.remember(value(3U)) &&
              !seen.contains(value(1U)) && seen.size() == 2U,
              "seen cache remains bounded");

        std::cout << checks << " P2P protocol checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
