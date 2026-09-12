#include "onuros/stage7_orchard_network.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace onuros;

Hash256 value(std::uint32_t number) {
    Hash256 result{};
    for (std::size_t i = 0U; i < 4U; ++i)
        result[result.size() - 1U - i] =
            static_cast<std::uint8_t>(number >> (8U * i));
    return result;
}

Hash256 chain_id() {
    const std::string name = "onuros-stage7-private-relay-gate";
    return double_sha256(
        std::vector<std::uint8_t>(name.begin(), name.end()));
}

int mock_orchard_verify(const std::uint8_t*, std::size_t,
                        const std::uint8_t*) {
    return static_cast<int>(OrchardFfiStatus::verified);
}

OrchardNetworkAdmission make_admission(const ShieldedState& state) {
    return OrchardNetworkAdmission(
        &mock_orchard_verify, state,
        {64U * 1024U, 4U, 16U * 1024U},
        {16U, 1U << 20U, 64U, 64U * 1024U, 4U},
        16U, 1U << 20U);
}

TransactionEnvelope make_transaction(std::uint8_t selector,
                                     const Hash256& root) {
    PrivateTransactionBundle bundle;
    bundle.anchor = root;
    bundle.value_balance = 1;
    bundle.fee = 1;
    PrivateActionBundle action;
    action.value_commitment = value(100U + selector);
    action.nullifier = value(200U + selector);
    action.randomized_key = value(300U + selector);
    action.note_commitment = value(400U + selector);
    action.ephemeral_key = value(500U + selector);
    bundle.actions.push_back(action);
    bundle.proof.assign(
        orchard_proof_base_size + orchard_proof_per_action_size, selector);
    return make_private_transaction(bundle);
}

bool send_all(TcpConnection& connection,
              const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0U;
    for (unsigned attempt = 0U;
         attempt < 10'000U && offset < bytes.size(); ++attempt) {
        const auto sent = connection.send_some(bytes.data() + offset,
                                                bytes.size() - offset);
        if (sent.status == SocketIoStatus::ok) offset += sent.bytes;
        else if (sent.status != SocketIoStatus::would_block) return false;
        if (sent.status == SocketIoStatus::would_block)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return offset == bytes.size();
}

void send_frame(TcpConnection& connection, P2pMessageType type,
                std::uint64_t request_id,
                const std::vector<std::uint8_t>& payload = {}) {
    if (!send_all(connection, encode_p2p_frame(
            {stage7_protocol_version, type, request_id, payload})))
        throw std::runtime_error("peer send failed");
}

P2pFrame receive_frame(TcpConnection& connection,
                       FrameStreamDecoder& stream) {
    std::array<std::uint8_t, 4096U> bytes{};
    for (unsigned attempt = 0U; attempt < 10'000U; ++attempt) {
        const auto parsed = stream.next();
        if (parsed.status == FrameStreamStatus::frame_ready)
            return parsed.frame;
        if (parsed.status == FrameStreamStatus::failed)
            throw std::runtime_error("invalid peer frame");
        const auto received = connection.receive_some(bytes.data(), bytes.size());
        if (received.status == SocketIoStatus::ok) {
            if (!stream.feed(bytes.data(), received.bytes))
                throw std::runtime_error("peer receive queue exceeded limit");
        } else if (received.status != SocketIoStatus::would_block) {
            throw std::runtime_error("peer disconnected");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("peer receive timeout");
}

void handshake(TcpConnection& connection, FrameStreamDecoder& stream,
               std::uint64_t nonce) {
    HelloMessage local;
    local.chain_id = chain_id();
    local.genesis_hash = value(1U);
    local.services = p2p_service_full_node | p2p_service_compact_relay;
    local.node_nonce = nonce;
    send_frame(connection, P2pMessageType::hello, 1U, encode_hello(local));
    const auto remote = receive_frame(connection, stream);
    HandshakePolicy policy;
    policy.chain_id = local.chain_id;
    policy.genesis_hash = local.genesis_hash;
    policy.local_nonce = nonce;
    policy.required_services = p2p_service_full_node;
    PeerSession session(policy);
    if (session.receive(remote, false) != PeerSessionError::none ||
        session.state() != PeerSessionState::established)
        throw std::runtime_error("peer handshake rejected");
}

std::optional<TcpConnection> connect_with_retry(std::uint16_t port) {
    for (unsigned attempt = 0U; attempt < 5000U; ++attempt) {
        auto connection = TcpConnection::connect_ipv4("127.0.0.1", port);
        if (connection) return connection;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

TcpConnection accept_with_retry(TcpListener& listener) {
    for (unsigned attempt = 0U; attempt < 10'000U; ++attempt) {
        auto connection = listener.accept_one();
        if (connection) return std::move(*connection);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("peer accept timeout");
}

Hash256 admit_one(OrchardNetworkAdmission& admission,
                  const P2pFrame& frame, EventPeerId peer) {
    const auto decoded = decode_network_transactions(
        frame.payload, NetworkTransactionBatchLimits{});
    if (frame.type != P2pMessageType::transactions ||
        !decoded.accepted() || decoded.transactions.size() != 1U)
        throw std::runtime_error("expected one private transaction");
    const auto result = admission.handle_frame(peer, frame);
    if (!result || !result->accepted() ||
        result->accepted_transactions != 1U ||
        admission.mempool().size() != 1U ||
        admission.relay_pool().size() != 1U)
        throw std::runtime_error("private transaction admission failed");
    return transaction_id(decoded.transactions.front());
}

void run_server(std::uint16_t port) {
    const ShieldedState state(value(1U), value(2U));
    auto admission = make_admission(state);
    auto listener = TcpListener::listen_loopback(port);
    if (!listener) throw std::runtime_error("listen failed");
    std::cout << "READY port=" << listener->port() << std::endl;

    auto origin = accept_with_retry(*listener);
    FrameStreamDecoder origin_stream({}, 256U * 1024U);
    handshake(origin, origin_stream, 10'001U);
    std::cout << "ORIGIN_CONNECTED" << std::endl;

    auto observer = accept_with_retry(*listener);
    FrameStreamDecoder observer_stream({}, 256U * 1024U);
    handshake(observer, observer_stream, 10'002U);
    send_frame(origin, P2pMessageType::ping, 9U);
    const auto transaction_frame = receive_frame(origin, origin_stream);
    const auto id = admit_one(admission, transaction_frame, 1U);
    std::cout << "ADMITTED node=relay txid=" << hash_hex(id)
              << " mempool=1 relay=1" << std::endl;
    send_frame(observer, P2pMessageType::transactions, 10U,
               transaction_frame.payload);
    const auto acknowledgement = receive_frame(observer, observer_stream);
    if (acknowledgement.type != P2pMessageType::pong ||
        acknowledgement.request_id != 10U ||
        acknowledgement.payload != std::vector<std::uint8_t>(
            id.begin(), id.end()))
        throw std::runtime_error("observer acknowledgement mismatch");
    std::cout << "RELAY_COMPLETE txid=" << hash_hex(id) << std::endl;
}

void run_origin(std::uint16_t port, std::uint8_t selector) {
    const ShieldedState state(value(1U), value(2U));
    auto admission = make_admission(state);
    auto connection = connect_with_retry(port);
    if (!connection) throw std::runtime_error("origin connect failed");
    FrameStreamDecoder stream({}, 256U * 1024U);
    handshake(*connection, stream, 20'001U);
    const auto request = receive_frame(*connection, stream);
    if (request.type != P2pMessageType::ping || request.request_id != 9U)
        throw std::runtime_error("origin start request mismatch");
    const auto transaction = make_transaction(selector, state.root());
    P2pFrame frame{stage7_protocol_version, P2pMessageType::transactions,
                   9U, encode_network_transactions({transaction})};
    const auto id = admit_one(admission, frame, 2U);
    std::cout << "ADMITTED node=origin txid=" << hash_hex(id)
              << " mempool=1 relay=1" << std::endl;
    send_frame(*connection, frame.type, frame.request_id, frame.payload);
}

void run_observer(std::uint16_t port) {
    const ShieldedState state(value(1U), value(2U));
    auto admission = make_admission(state);
    auto connection = connect_with_retry(port);
    if (!connection) throw std::runtime_error("observer connect failed");
    FrameStreamDecoder stream({}, 256U * 1024U);
    handshake(*connection, stream, 20'002U);
    const auto frame = receive_frame(*connection, stream);
    const auto id = admit_one(admission, frame, 3U);
    std::cout << "ADMITTED node=observer txid=" << hash_hex(id)
              << " mempool=1 relay=1" << std::endl;
    send_frame(*connection, P2pMessageType::pong, frame.request_id,
               std::vector<std::uint8_t>(id.begin(), id.end()));
}

std::string argument(int argc, char** argv, const std::string& name,
                     const std::string& fallback = {}) {
    for (int i = 1; i + 1 < argc; ++i)
        if (argv[i] == name) return argv[i + 1];
    return fallback;
}
} // namespace

int main(int argc, char** argv) {
    try {
        const auto role = argument(argc, argv, "--role");
        const auto port_text = argument(argc, argv, "--port", "38465");
        const auto port_value = std::stoul(port_text);
        if (port_value == 0U || port_value > 65'535U)
            throw std::runtime_error("invalid port");
        const auto port = static_cast<std::uint16_t>(port_value);
        if (role == "server") run_server(port);
        else if (role == "origin") {
            const auto selector = std::stoul(
                argument(argc, argv, "--selector", "1"));
            if (selector == 0U || selector > 255U)
                throw std::runtime_error("invalid selector");
            run_origin(port, static_cast<std::uint8_t>(selector));
        } else if (role == "observer") run_observer(port);
        else throw std::runtime_error("expected server, origin or observer role");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
