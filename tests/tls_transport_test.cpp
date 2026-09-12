#include "onuros/tls_transport.hpp"
#include "onuros/peer_event_loop.hpp"

#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace onuros;

namespace {
unsigned checks = 0U;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 6) return 2;
        auto server_context = TlsContext::mutual(argv[1], argv[2], argv[5]);
        auto client_context = TlsContext::mutual(argv[3], argv[4], argv[5]);
        check(server_context && client_context, "mutual TLS contexts load");
        SocketRuntime runtime;
        auto listener = TcpListener::listen_loopback();
        check(runtime.ready() && listener, "TLS loopback listener opens");
        auto client_socket = TcpConnection::connect_ipv4("127.0.0.1", listener->port());
        std::optional<TcpConnection> server_socket;
        for (unsigned i = 0U; i < 2'000U && !server_socket; ++i) {
            server_socket = listener->accept_one();
            if (!server_socket) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(client_socket && server_socket, "TLS TCP peers connect");
        auto client = std::make_unique<TlsPeerTransport>(
            *client_context, std::move(*client_socket), TlsRole::client, "onuros-server");
        auto server = std::make_unique<TlsPeerTransport>(
            *server_context, std::move(*server_socket), TlsRole::server, "onuros-client");
        for (unsigned i = 0U; i < 10'000U &&
                (!client->authenticated() || !server->authenticated()); ++i) {
            if (!client->authenticated() && client->handshake() == TlsStatus::error)
                throw std::runtime_error("client TLS handshake failed");
            if (!server->authenticated() && server->handshake() == TlsStatus::error)
                throw std::runtime_error("server TLS handshake failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(client->authenticated() && server->authenticated(),
              "TLS 1.3 mutual authentication completes");
        check(client->cipher() != nullptr && server->cipher() != nullptr,
              "authenticated cipher negotiated");
        const std::array<std::uint8_t, 5> message{{1U, 2U, 3U, 4U, 5U}};
        TlsIoResult sent;
        for (unsigned i = 0U; i < 2'000U && sent.status != TlsStatus::ok; ++i) {
            const auto result = client->send_some(message.data(), message.size());
            if (result.status == SocketIoStatus::error)
                throw std::runtime_error("TLS send failed");
            if (result.status == SocketIoStatus::ok)
                sent = {TlsStatus::ok, result.bytes};
        }
        std::array<std::uint8_t, 5> received{};
        TlsIoResult read;
        for (unsigned i = 0U; i < 2'000U && read.status != TlsStatus::ok; ++i) {
            const auto result = server->receive_some(received.data(), received.size());
            if (result.status == SocketIoStatus::error)
                throw std::runtime_error("TLS read failed");
            if (result.status == SocketIoStatus::ok)
                read = {TlsStatus::ok, result.bytes};
        }
        check(sent.bytes == message.size() && read.bytes == message.size() &&
              received == message, "encrypted payload round trip is exact");

        HandshakePolicy policy;
        policy.chain_id[0] = 1U;
        policy.genesis_hash[0] = 2U;
        policy.local_nonce = 90U;
        policy.required_services = p2p_service_compact_relay;
        policy.require_authenticated_transport = true;
        PeerEventLoopLimits loop_limits;
        loop_limits.maximum_peers = 1U;
        PeerEventLoop loop(loop_limits);
        check(loop.add_peer(1U, std::move(server), policy, 0U) ==
                  PeerLoopError::none,
              "authenticated TLS transport enters event loop");
        HelloMessage hello;
        hello.chain_id = policy.chain_id;
        hello.genesis_hash = policy.genesis_hash;
        hello.node_nonce = 91U;
        hello.services = p2p_service_compact_relay |
                         p2p_service_authenticated_transport;
        const auto hello_wire = encode_p2p_frame({stage7_protocol_version,
            P2pMessageType::hello, 1U, encode_hello(hello)});
        std::size_t hello_offset = 0U;
        for (unsigned i = 0U; i < 2'000U && hello_offset < hello_wire.size(); ++i) {
            const auto result = client->send_some(hello_wire.data() + hello_offset,
                                                  hello_wire.size() - hello_offset);
            if (result.status == SocketIoStatus::ok) hello_offset += result.bytes;
            else if (result.status != SocketIoStatus::would_block)
                throw std::runtime_error("encrypted hello send failed");
        }
        unsigned frames = 0U;
        for (unsigned i = 0U; i < 2'000U && frames == 0U; ++i) {
            loop.tick(1U, [&](EventPeerId, const P2pFrame& frame) {
                if (frame.type == P2pMessageType::hello) ++frames;
            });
        }
        check(frames == 1U && loop.peer_count() == 1U &&
              loop.stats().protocol_failures == 0U,
              "event loop accepts handshake only over authenticated TLS");
        std::cout << checks << " mutual-TLS checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
