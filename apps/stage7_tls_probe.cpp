#include "onuros/p2p_transport.hpp"
#include "onuros/tls_transport.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace onuros;

namespace {
constexpr unsigned maximum_attempts = 15'000U;
constexpr auto retry_delay = std::chrono::milliseconds(2);

std::uint16_t parse_port(const char* text) {
    char* end = nullptr;
    const auto value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value == 0UL || value > 65'535UL)
        throw std::runtime_error("invalid port");
    return static_cast<std::uint16_t>(value);
}

void handshake(TlsPeerTransport& transport) {
    for (unsigned attempt = 0U; attempt < maximum_attempts; ++attempt) {
        const auto status = transport.handshake();
        if (status == TlsStatus::ok) return;
        if (status == TlsStatus::error) throw std::runtime_error("TLS handshake failed");
        std::this_thread::sleep_for(retry_delay);
    }
    throw std::runtime_error("TLS handshake timeout");
}

void send_all(TlsPeerTransport& transport, const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0U;
    for (unsigned attempt = 0U;
         attempt < maximum_attempts && offset < bytes.size(); ++attempt) {
        const auto result = transport.send_some(bytes.data() + offset,
                                                bytes.size() - offset);
        if (result.status == SocketIoStatus::ok) offset += result.bytes;
        else if (result.status != SocketIoStatus::would_block)
            throw std::runtime_error("TLS write failed");
        if (offset < bytes.size()) std::this_thread::sleep_for(retry_delay);
    }
    if (offset != bytes.size()) throw std::runtime_error("TLS write timeout");
}

P2pFrame receive_frame(TlsPeerTransport& transport) {
    FrameStreamDecoder decoder(P2pFrameLimits{}, 512U * 1024U);
    std::vector<std::uint8_t> buffer(4096U);
    for (unsigned attempt = 0U; attempt < maximum_attempts; ++attempt) {
        const auto result = transport.receive_some(buffer.data(), buffer.size());
        if (result.status == SocketIoStatus::ok) {
            if (!decoder.feed(buffer.data(), result.bytes))
                throw std::runtime_error("TLS frame buffer limit");
            const auto frame = decoder.next();
            if (frame.status == FrameStreamStatus::frame_ready) return frame.frame;
            if (frame.status == FrameStreamStatus::failed)
                throw std::runtime_error("invalid checksummed P2P frame");
        } else if (result.status != SocketIoStatus::would_block) {
            throw std::runtime_error("TLS read failed");
        }
        std::this_thread::sleep_for(retry_delay);
    }
    throw std::runtime_error("TLS read timeout");
}

int run_server(char** argv) {
    const auto port = parse_port(argv[3]);
    auto context = TlsContext::mutual(argv[4], argv[5], argv[6]);
    if (!context) throw std::runtime_error("server TLS material failed to load");
    auto listener = TcpListener::listen_ipv4(argv[2], port);
    if (!listener) throw std::runtime_error("server listen failed");
    std::cout << "READY bind=" << argv[2] << " port=" << listener->port() << '\n'
              << std::flush;
    std::optional<TcpConnection> connection;
    for (unsigned attempt = 0U; attempt < maximum_attempts && !connection; ++attempt) {
        connection = listener->accept_one();
        if (!connection) std::this_thread::sleep_for(retry_delay);
    }
    if (!connection) throw std::runtime_error("server accept timeout");
    TlsPeerTransport transport(*context, std::move(*connection), TlsRole::server,
                               argv[7]);
    handshake(transport);
    const auto ping = receive_frame(transport);
    if (ping.type != P2pMessageType::ping || ping.request_id == 0U ||
        ping.payload.empty() || ping.payload.size() > 64U)
        throw std::runtime_error("invalid TLS probe request");
    send_all(transport, encode_p2p_frame({stage7_protocol_version,
        P2pMessageType::pong, ping.request_id, ping.payload}));
    std::cout << "tls_server=PASS peer_name=" << argv[7]
              << " request_id=" << ping.request_id
              << " payload_bytes=" << ping.payload.size()
              << " cipher=" << transport.cipher() << '\n';
    return 0;
}

int run_client(char** argv) {
    const auto port = parse_port(argv[3]);
    auto context = TlsContext::mutual(argv[4], argv[5], argv[6]);
    if (!context) throw std::runtime_error("client TLS material failed to load");
    std::optional<TcpConnection> connection;
    for (unsigned attempt = 0U; attempt < maximum_attempts && !connection; ++attempt) {
        connection = TcpConnection::connect_ipv4(argv[2], port);
        if (!connection) std::this_thread::sleep_for(retry_delay);
    }
    if (!connection) throw std::runtime_error("client connect timeout");
    TlsPeerTransport transport(*context, std::move(*connection), TlsRole::client,
                               argv[7]);
    handshake(transport);
    const std::string probe(argv[8]);
    if (probe.empty() || probe.size() > 64U)
        throw std::runtime_error("probe ID must contain 1..64 bytes");
    const std::vector<std::uint8_t> payload(probe.begin(), probe.end());
    constexpr std::uint64_t request_id = 1U;
    send_all(transport, encode_p2p_frame({stage7_protocol_version,
        P2pMessageType::ping, request_id, payload}));
    const auto pong = receive_frame(transport);
    if (pong.type != P2pMessageType::pong || pong.request_id != request_id ||
        pong.payload != payload)
        throw std::runtime_error("TLS probe response mismatch");
    std::cout << "tls_client=PASS peer_name=" << argv[7]
              << " request_id=" << pong.request_id
              << " payload_bytes=" << pong.payload.size()
              << " cipher=" << transport.cipher() << '\n';
    return 0;
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) return 2;
        SocketRuntime runtime;
        if (!runtime.ready()) throw std::runtime_error("socket runtime unavailable");
        const std::string mode(argv[1]);
        if (mode == "server" && argc == 8) return run_server(argv);
        if (mode == "client" && argc == 9) return run_client(argv);
        std::cerr << "usage: " << argv[0]
                  << " server BIND PORT CERT KEY CA EXPECTED_CLIENT_NAME\n"
                  << "       " << argv[0]
                  << " client ADDRESS PORT CERT KEY CA EXPECTED_SERVER_NAME PROBE_ID\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "tls_probe=FAIL reason=" << error.what() << '\n';
        return 1;
    }
}
