#include "onuros/tls_transport.hpp"

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
        TlsConnection client(*client_context, *client_socket, TlsRole::client);
        TlsConnection server(*server_context, *server_socket, TlsRole::server);
        for (unsigned i = 0U; i < 10'000U &&
                (!client.authenticated() || !server.authenticated()); ++i) {
            if (!client.authenticated() && client.handshake() == TlsStatus::error)
                throw std::runtime_error("client TLS handshake failed");
            if (!server.authenticated() && server.handshake() == TlsStatus::error)
                throw std::runtime_error("server TLS handshake failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(client.authenticated() && server.authenticated(),
              "TLS 1.3 mutual authentication completes");
        check(client.cipher() != nullptr && server.cipher() != nullptr,
              "authenticated cipher negotiated");
        const std::array<std::uint8_t, 5> message{{1U, 2U, 3U, 4U, 5U}};
        TlsIoResult sent;
        for (unsigned i = 0U; i < 2'000U && sent.status != TlsStatus::ok; ++i) {
            sent = client.send_some(message.data(), message.size());
            if (sent.status == TlsStatus::error) throw std::runtime_error("TLS send failed");
        }
        std::array<std::uint8_t, 5> received{};
        TlsIoResult read;
        for (unsigned i = 0U; i < 2'000U && read.status != TlsStatus::ok; ++i) {
            read = server.receive_some(received.data(), received.size());
            if (read.status == TlsStatus::error) throw std::runtime_error("TLS read failed");
        }
        check(sent.bytes == message.size() && read.bytes == message.size() &&
              received == message, "encrypted payload round trip is exact");
        std::cout << checks << " mutual-TLS checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
