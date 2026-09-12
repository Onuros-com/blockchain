#include "onuros/peer_event_loop.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace onuros;

namespace {
unsigned checks = 0U;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}
Hash256 hash(std::uint8_t value) { Hash256 result{}; result[0] = value; return result; }

class FakeTransport final : public PeerTransport {
public:
    std::vector<std::uint8_t> incoming;
    std::vector<std::uint8_t> outgoing;
    std::size_t read_offset = 0U;
    std::size_t maximum_io = 11U;
    bool authenticated_value = true;
    bool closed = false;

    SocketIoResult send_some(const std::uint8_t* data, std::size_t size) override {
        if (closed) return {SocketIoStatus::closed, 0U};
        const auto count = std::min(size, maximum_io);
        outgoing.insert(outgoing.end(), data, data + count);
        return {SocketIoStatus::ok, count};
    }
    SocketIoResult receive_some(std::uint8_t* data, std::size_t size) override {
        if (closed) return {SocketIoStatus::closed, 0U};
        if (read_offset == incoming.size()) return {SocketIoStatus::would_block, 0U};
        const auto count = std::min({size, maximum_io, incoming.size() - read_offset});
        std::memcpy(data, incoming.data() + read_offset, count);
        read_offset += count;
        return {SocketIoStatus::ok, count};
    }
    bool authenticated() const noexcept override { return authenticated_value; }
};

HandshakePolicy policy(std::uint64_t nonce) {
    HandshakePolicy result;
    result.chain_id = hash(1U);
    result.genesis_hash = hash(2U);
    result.local_nonce = nonce;
    result.required_services = p2p_service_compact_relay;
    result.require_authenticated_transport = true;
    return result;
}

std::vector<std::uint8_t> peer_input(std::uint64_t nonce) {
    HelloMessage hello;
    hello.chain_id = hash(1U);
    hello.genesis_hash = hash(2U);
    hello.node_nonce = nonce;
    hello.services = p2p_service_compact_relay |
                     p2p_service_authenticated_transport;
    auto result = encode_p2p_frame({stage7_protocol_version,
        P2pMessageType::hello, 1U, encode_hello(hello)});
    const auto ping = encode_p2p_frame({stage7_protocol_version,
        P2pMessageType::ping, 2U, {7U}});
    result.insert(result.end(), ping.begin(), ping.end());
    return result;
}
}

int main() {
    try {
        PeerEventLoopLimits limits;
        limits.maximum_peers = 3U;
        limits.maximum_receive_buffer_bytes = 1024U;
        limits.maximum_read_bytes_per_tick = 512U;
        limits.maximum_write_bytes_per_tick = 32U;
        limits.maximum_frames_per_tick = 1U;
        limits.resources.maximum_queued_bytes = 512U;
        limits.deadlines.handshake_timeout_seconds = 5U;
        limits.deadlines.idle_timeout_seconds = 20U;
        PeerEventLoop loop(limits);
        std::vector<FakeTransport*> transports;
        for (EventPeerId id = 1U; id <= 3U; ++id) {
            auto transport = std::make_unique<FakeTransport>();
            transport->incoming = peer_input(100U + id);
            transports.push_back(transport.get());
            check(loop.add_peer(id, std::move(transport), policy(200U + id), 0U) ==
                      PeerLoopError::none,
                  "simultaneous authenticated peer added");
        }
        check(loop.add_peer(4U, std::make_unique<FakeTransport>(), policy(204U), 0U) ==
                  PeerLoopError::peer_limit,
              "global peer limit enforced");
        std::vector<EventPeerId> delivered;
        loop.tick(1U, [&](EventPeerId peer, const P2pFrame&) {
            delivered.push_back(peer);
        });
        check(delivered == std::vector<EventPeerId>({1U, 2U, 3U}),
              "one-frame budget is fair across peers");
        loop.tick(2U, [&](EventPeerId peer, const P2pFrame& frame) {
            delivered.push_back(peer);
            check(frame.type == P2pMessageType::ping, "established peer delivers ping");
            check(loop.queue(peer, {stage7_protocol_version, P2pMessageType::pong,
                                    frame.request_id, frame.payload}) ==
                      PeerLoopError::none,
                  "callback queues bounded response");
        });
        check(delivered.size() == 6U, "all simultaneous peers make progress");
        for (unsigned tick = 0U; tick < 20U; ++tick) loop.tick(3U + tick, {});
        for (const auto* transport : transports) {
            const auto response = decode_p2p_frame(transport->outgoing, {});
            check(response.error == P2pFrameError::none &&
                  response.frame.type == P2pMessageType::pong,
                  "partial writes drain exact framed response");
        }
        check(loop.stats().received_frames == 6U && loop.stats().sent_bytes != 0U,
              "event-loop traffic metrics recorded");

        PeerEventLoop timeout_loop(limits);
        check(timeout_loop.add_peer(9U, std::make_unique<FakeTransport>(),
                                    policy(999U), 0U) == PeerLoopError::none,
              "silent peer added");
        timeout_loop.tick(5U, {});
        check(timeout_loop.peer_count() == 0U &&
              timeout_loop.stats().timed_out_peers == 1U,
              "handshake timeout disconnects silent peer");

        PeerEventLoop malformed_loop(limits);
        auto malformed = std::make_unique<FakeTransport>();
        malformed->incoming = peer_input(500U);
        malformed->incoming[0] ^= 1U;
        check(malformed_loop.add_peer(10U, std::move(malformed), policy(501U), 0U) ==
                  PeerLoopError::none,
              "malformed peer added before parsing");
        malformed_loop.tick(1U, {});
        check(malformed_loop.peer_count() == 0U &&
              malformed_loop.stats().protocol_failures == 1U,
              "malformed framing disconnects only offending peer");

        PeerEventLoop policy_loop(limits);
        auto policy_transport = std::make_unique<FakeTransport>();
        policy_transport->incoming = peer_input(700U);
        check(policy_loop.add_peer(12U, std::move(policy_transport),
                                   policy(701U), 0U) ==
                  PeerLoopError::none,
              "policy peer added");
        const auto disconnect_ping =
            [](EventPeerId, const P2pFrame& frame) {
                return frame.type == P2pMessageType::ping
                    ? PeerFrameAction::disconnect : PeerFrameAction::keep;
            };
        policy_loop.tick_with_policy(1U, disconnect_ping);
        check(policy_loop.peer_count() == 1U,
              "policy retains accepted handshake");
        policy_loop.tick_with_policy(2U, disconnect_ping);
        check(policy_loop.peer_count() == 0U &&
              policy_loop.stats().policy_disconnects == 1U,
              "frame policy disconnects offending peer");

        PeerEventLoopLimits queue_limits = limits;
        queue_limits.resources.maximum_queued_bytes = 60U;
        PeerEventLoop queue_loop(queue_limits);
        check(queue_loop.add_peer(11U, std::make_unique<FakeTransport>(),
                                  policy(600U), 0U) == PeerLoopError::none &&
              queue_loop.queue(11U, {stage7_protocol_version, P2pMessageType::ping,
                                     1U, std::vector<std::uint8_t>(9U)}) ==
                  PeerLoopError::queue_limit,
              "oversized send queue applies backpressure");
        queue_loop.shutdown();
        check(queue_loop.peer_count() == 0U, "clean shutdown releases all peers");

        std::cout << checks << " peer-event-loop checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
