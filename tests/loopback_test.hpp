#pragma once

#include "onuros/p2p_transport.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <thread>

namespace onuros::test {

struct LoopbackPair {
    TcpListener listener;
    TcpConnection client;
    TcpConnection server;
};

inline std::optional<LoopbackPair> connect_loopback_pair() {
    constexpr std::uint16_t first_fallback_port = 39'450U;
    constexpr std::uint16_t fallback_port_count = 32U;

    for (std::uint16_t candidate = 0U;
         candidate <= fallback_port_count; ++candidate) {
        const auto requested_port = candidate == 0U
            ? 0U
            : static_cast<std::uint16_t>(first_fallback_port + candidate - 1U);
        auto listener = TcpListener::listen_loopback(requested_port);
        if (!listener) continue;

        std::optional<TcpConnection> client;
        for (unsigned attempt = 0U; attempt < 50U && !client; ++attempt) {
            client = TcpConnection::connect_ipv4("127.0.0.1", listener->port());
            if (!client)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!client) continue;

        std::optional<TcpConnection> server;
        for (unsigned attempt = 0U; attempt < 2'000U && !server; ++attempt) {
            server = listener->accept_one();
            if (!server)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (server)
            return LoopbackPair{std::move(*listener), std::move(*client),
                                std::move(*server)};
    }
    return std::nullopt;
}

} // namespace onuros::test
