#include "onuros/download_coordinator.hpp"
#include "onuros/header_sync.hpp"
#include "onuros/local_node.hpp"
#include "onuros/p2p_transport.hpp"
#include "onuros/stage7_relay.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace onuros;

namespace {

Hash256 test_pow(const BlockHeader& header) {
    return double_sha256(encode_block_header(header));
}

Hash256 test_chain_id() {
    const std::string name = "onuros-stage7-private-test-chain";
    return double_sha256(std::vector<std::uint8_t>(name.begin(), name.end()));
}

LocalNodeParameters parameters() {
    const auto limit = decode_compact_target(0x207fffffU);
    if (!limit) throw std::runtime_error("test proof-of-work target is invalid");
    LocalNodeParameters value;
    value.validation_limits = {1U, 1U, 1U << 20U, 512U, 4096U};
    value.decode_limits = {1U << 20U, 512U, 4096U};
    value.difficulty = {60U, 60U, 4U, *limit};
    value.max_database_bytes = 16U << 20U;
    return value;
}

Block ensure_genesis(LocalNode& node) {
    if (node.store().index().active_tip() != nullptr) {
        const auto* genesis = node.store().blocks().empty()
            ? nullptr : &node.store().blocks().front().block;
        if (genesis == nullptr)
            throw std::runtime_error("unexpected test database state");
        return *genesis;
    }
    auto genesis = node.make_candidate({{1U, {0U}}}, 100U);
    if (!genesis || node.mine(*genesis, 100U, 100'000U).error !=
            LocalNodeError::none)
        throw std::runtime_error("could not create deterministic genesis");
    return *genesis;
}

void ensure_relay_blocks(LocalNode& node, Height target_height) {
    const auto* tip = node.store().index().active_tip();
    if (tip == nullptr) throw std::runtime_error("genesis is missing");
    while (tip->height < target_height) {
        const auto next_height = tip->height + 1U;
        std::vector<TransactionEnvelope> transactions;
        for (std::uint8_t i = 1U; i <= 20U; ++i) {
            std::vector<std::uint8_t> body(128U, i);
            body[0] = static_cast<std::uint8_t>(next_height);
            transactions.push_back({1U, std::move(body)});
        }
        const auto timestamp = 100U + next_height * 60U;
        auto block = node.make_candidate(std::move(transactions), timestamp);
        if (!block || node.mine(*block, timestamp, 100'000U).error !=
                LocalNodeError::none)
            throw std::runtime_error("could not mine relay block sequence");
        tip = node.store().index().active_tip();
    }
    if (tip->height != target_height)
        throw std::runtime_error("server database exceeds requested height");
}

bool send_all(TcpConnection& connection, const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0U;
    for (unsigned attempt = 0U; attempt < 10'000U && offset < bytes.size(); ++attempt) {
        const auto result = connection.send_some(bytes.data() + offset,
                                                  bytes.size() - offset);
        if (result.status == SocketIoStatus::ok) offset += result.bytes;
        else if (result.status != SocketIoStatus::would_block) return false;
        if (result.status == SocketIoStatus::would_block)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return offset == bytes.size();
}

P2pFrame receive_frame(TcpConnection& connection, FrameStreamDecoder& stream) {
    std::array<std::uint8_t, 4096U> bytes{};
    for (unsigned attempt = 0U; attempt < 10'000U; ++attempt) {
        const auto parsed = stream.next();
        if (parsed.status == FrameStreamStatus::frame_ready) return parsed.frame;
        if (parsed.status == FrameStreamStatus::failed)
            throw std::runtime_error("peer sent an invalid frame");
        const auto received = connection.receive_some(bytes.data(), bytes.size());
        if (received.status == SocketIoStatus::ok) {
            if (!stream.feed(bytes.data(), received.bytes))
                throw std::runtime_error("peer receive queue exceeded limit");
        } else if (received.status != SocketIoStatus::would_block) {
            throw std::runtime_error("peer disconnected during message");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("peer message timeout");
}

void send_frame(TcpConnection& connection, P2pMessageType type,
                std::uint64_t request_id,
                const std::vector<std::uint8_t>& payload) {
    const auto encoded = encode_p2p_frame(
        {stage7_protocol_version, type, request_id, payload});
    if (!send_all(connection, encoded))
        throw std::runtime_error("peer send failed");
}

void exchange_hello(TcpConnection& connection, FrameStreamDecoder& stream,
                    const Hash256& genesis, std::uint64_t local_nonce,
                    Height best_height) {
    HelloMessage hello;
    hello.chain_id = test_chain_id();
    hello.genesis_hash = genesis;
    hello.services = p2p_service_full_node | p2p_service_compact_relay;
    hello.node_nonce = local_nonce;
    hello.best_height = best_height;
    send_frame(connection, P2pMessageType::hello, 1U, encode_hello(hello));
    const auto remote_frame = receive_frame(connection, stream);
    HandshakePolicy policy;
    policy.chain_id = hello.chain_id;
    policy.genesis_hash = genesis;
    policy.local_nonce = local_nonce;
    policy.required_services = p2p_service_compact_relay;
    PeerSession session(policy);
    if (session.receive(remote_frame, false) != PeerSessionError::none ||
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

void serve_peer(TcpConnection& connection, const LocalNode& node,
                const Block& genesis, std::uint64_t nonce) {
    FrameStreamDecoder stream({}, 2U * 1024U * 1024U);
    const auto* tip = node.store().index().active_tip();
    if (tip == nullptr) throw std::runtime_error("server tip is missing");
    exchange_hello(connection, stream, block_id(genesis.header), nonce, tip->height);

    const auto header_request_frame = receive_frame(connection, stream);
    if (header_request_frame.type != P2pMessageType::get_headers)
        throw std::runtime_error("expected header locator request");
    const auto header_request = decode_header_request(
        header_request_frame.payload, 32U);
    if (!header_request) throw std::runtime_error("invalid header locator request");
    const auto& active = node.active_state().chain;
    std::optional<std::size_t> matched;
    for (const auto& locator : header_request->locator) {
        for (std::size_t position = active.size(); position != 0U; --position) {
            if (active[position - 1U] == locator) {
                matched = position - 1U;
                break;
            }
        }
        if (matched) break;
    }
    if (!matched) throw std::runtime_error("header locator has no common ancestor");
    std::vector<BlockHeader> headers;
    std::vector<const Block*> blocks;
    for (std::size_t position = *matched + 1U; position < active.size(); ++position) {
        const auto* stored = node.store().find(active[position]);
        if (stored == nullptr) throw std::runtime_error("active block is missing");
        headers.push_back(stored->block.header);
        blocks.push_back(&stored->block);
        if (header_request->stop != Hash256{} &&
            active[position] == header_request->stop)
            break;
    }
    send_frame(connection, P2pMessageType::headers,
               header_request_frame.request_id, encode_headers(headers));

    std::uint64_t request_id = header_request_frame.request_id + 1U;
    for (const auto* block : blocks) {
        send_frame(connection, P2pMessageType::compact_block, request_id,
            encode_compact_block_announcement(
                make_compact_block_announcement(*block)));
        const auto request_frame = receive_frame(connection, stream);
        if (request_frame.type != P2pMessageType::get_block_transactions ||
            request_frame.request_id != request_id)
            throw std::runtime_error("expected missing transaction request");
        const auto request = decode_missing_transaction_request(
            request_frame.payload, 512U);
        if (!request || request->block_identifier != block_id(block->header))
            throw std::runtime_error("invalid missing transaction request");
        const auto chunks = make_block_transaction_chunks(
            *block, request->indexes, 64U * 1024U);
        if (!chunks) throw std::runtime_error("could not construct response chunks");
        for (const auto& chunk : *chunks)
            send_frame(connection, P2pMessageType::block_transactions,
                       request_id, encode_block_transaction_chunk(chunk));
        ++request_id;
    }
}

void run_server(const std::filesystem::path& data, std::uint16_t port,
                unsigned peers, Height target_height) {
    LocalNode node(parameters(), test_pow);
    if (node.open(data).error != LocalNodeError::none)
        throw std::runtime_error("server database open failed");
    const auto genesis = ensure_genesis(node);
    ensure_relay_blocks(node, target_height);
    auto listener = TcpListener::listen_loopback(port);
    if (!listener) throw std::runtime_error("server listen failed");
    std::cout << "READY port=" << listener->port() << std::endl;
    for (unsigned peer = 0U; peer < peers; ++peer) {
        std::optional<TcpConnection> connection;
        for (unsigned attempt = 0U; attempt < 10'000U && !connection; ++attempt) {
            connection = listener->accept_one();
            if (!connection)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!connection) throw std::runtime_error("server accept timeout");
        serve_peer(*connection, node, genesis, 10'000U + peer);
    }
    std::cout << "SYNCED peers=" << peers
              << " tip=" << hash_hex(node.store().index().active_tip()->id)
              << std::endl;
}

void run_client(const std::filesystem::path& data, std::uint16_t port,
                std::uint64_t nonce, Height target_height) {
    LocalNode node(parameters(), test_pow);
    if (node.open(data).error != LocalNodeError::none)
        throw std::runtime_error("client database open failed");
    const auto genesis = ensure_genesis(node);
    const auto* existing = node.store().index().active_tip();
    if (existing != nullptr && existing->height == target_height) {
        std::cout << "RECOVERED height=" << target_height
                  << " tip=" << hash_hex(existing->id)
                  << std::endl;
        return;
    }
    auto connection = connect_with_retry(port);
    if (!connection) throw std::runtime_error("client connect timeout");
    FrameStreamDecoder stream({}, 2U * 1024U * 1024U);
    exchange_hello(*connection, stream, block_id(genesis.header), nonce,
                   existing == nullptr ? 0U : existing->height);
    if (existing == nullptr) throw std::runtime_error("local genesis is missing");
    send_frame(*connection, P2pMessageType::get_headers, 2U,
               encode_header_request({{existing->id}, {}}));
    const auto headers_frame = receive_frame(*connection, stream);
    if (headers_frame.type != P2pMessageType::headers ||
        headers_frame.request_id != 2U)
        throw std::runtime_error("expected header batch");
    const auto headers = decode_headers(headers_frame.payload, 1'600U);
    if (!headers || headers->empty()) throw std::runtime_error("empty header batch");
    HeaderSyncLimits header_limits;
    header_limits.difficulty = parameters().difficulty;
    HeaderSyncChain header_chain(header_limits, test_pow);
    const auto* stored_tip = node.store().find(existing->id);
    if (stored_tip == nullptr ||
        header_chain.seed(stored_tip->block.header, existing->accumulated_work) !=
            HeaderSyncError::none)
        throw std::runtime_error("could not seed local header chain");
    const auto header_result = header_chain.accept(
        *headers, 100U + target_height * 60U);
    if (header_result.error != HeaderSyncError::none ||
        !header_result.stronger_tip ||
        header_chain.best_tip()->header.height != target_height)
        throw std::runtime_error("header chain validation failed");

    ValidatedRelayPool pool(512U, 1U << 20U);
    BlockDownloadCoordinator coordinator(1U, 30U);
    BlockChunkLimits limits;
    limits.maximum_chunk_bytes = 64U * 1024U;
    limits.maximum_transactions_per_chunk = 512U;
    limits.maximum_transaction_body_bytes = 4096U;
    limits.maximum_total_chunks = 512U;
    limits.maximum_total_transactions = 512U;
    limits.maximum_total_transfer_bytes = 1U << 20U;
    std::uint64_t request_id = 3U;
    for (const auto& expected_header : *headers) {
        const auto compact_frame = receive_frame(*connection, stream);
        if (compact_frame.type != P2pMessageType::compact_block ||
            compact_frame.request_id != request_id)
            throw std::runtime_error("expected compact block");
        const auto announcement = decode_compact_block_announcement(
            compact_frame.payload, 512U, 64U * 1024U);
        if (!announcement || !(announcement->header == expected_header))
            throw std::runtime_error("compact block was not header-approved");
        const auto identifier = block_id(announcement->header);
        if (coordinator.claim(identifier, 1U, 0U) !=
                DownloadClaimResult::claimed)
            throw std::runtime_error("duplicate block download rejected");
        CompactDownloadResult immediate;
        CompactBlockDownload download(*announcement, pool, limits,
            [](const TransactionEnvelope& transaction) {
                return transaction.version == 1U && transaction.body.size() == 128U;
            });
        const auto request = download.start(immediate);
        if (!request) throw std::runtime_error("expected missing transactions");
        send_frame(*connection, P2pMessageType::get_block_transactions, request_id,
                   encode_missing_transaction_request(*request));
        CompactDownloadResult completed;
        while (!completed.block) {
            const auto chunk_frame = receive_frame(*connection, stream);
            if (chunk_frame.type != P2pMessageType::block_transactions ||
                chunk_frame.request_id != request_id ||
                !coordinator.accepts_from(identifier, 1U, 0U))
                throw std::runtime_error("unexpected block response");
            const auto chunk = decode_block_transaction_chunk(
                chunk_frame.payload, limits);
            if (!chunk) throw std::runtime_error("invalid block response chunk");
            coordinator.record_useful(chunk_frame.payload.size());
            completed = download.add_chunk(*chunk);
            if (completed.error != CompactDownloadError::none)
                throw std::runtime_error("compact block reconstruction failed");
        }
        if (node.submit(*completed.block, expected_header.timestamp).error !=
                LocalNodeError::none)
            throw std::runtime_error("locally reconstructed block was rejected");
        coordinator.complete(identifier);
        ++request_id;
    }
    const auto* tip = node.store().index().active_tip();
    if (tip == nullptr || tip->height != target_height)
        throw std::runtime_error("client did not activate synchronized block");
    std::cout << "SYNCED height=" << target_height
              << " useful_bytes=" << coordinator.accounting().useful_bytes
              << " tip=" << hash_hex(tip->id) << std::endl;
}

void usage(const char* program) {
    std::cerr << "Usage: " << program
              << " --role server|client --data PATH --port PORT"
                 " [--peers COUNT] [--nonce VALUE] [--blocks HEIGHT]\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string role;
        std::filesystem::path data;
        std::uint16_t port = 0U;
        unsigned peers = 2U;
        std::uint64_t nonce = 20'000U;
        Height blocks = 3U;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--role" && i + 1 < argc) role = argv[++i];
            else if (argument == "--data" && i + 1 < argc) data = argv[++i];
            else if (argument == "--port" && i + 1 < argc) {
                const auto value = std::stoul(argv[++i]);
                if (value == 0U || value > 65'535U)
                    throw std::invalid_argument("invalid port");
                port = static_cast<std::uint16_t>(value);
            } else if (argument == "--peers" && i + 1 < argc) {
                peers = static_cast<unsigned>(std::stoul(argv[++i]));
            } else if (argument == "--nonce" && i + 1 < argc) {
                nonce = std::stoull(argv[++i]);
            } else if (argument == "--blocks" && i + 1 < argc) {
                blocks = std::stoull(argv[++i]);
                if (blocks == 0U) throw std::invalid_argument("invalid block count");
            } else {
                throw std::invalid_argument("unknown or incomplete argument");
            }
        }
        if (data.empty() || port == 0U || (role != "server" && role != "client"))
            throw std::invalid_argument("required argument missing");
        SocketRuntime runtime;
        if (!runtime.ready()) throw std::runtime_error("socket runtime failed");
        if (role == "server") run_server(data, port, peers, blocks);
        else run_client(data, port, nonce, blocks);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        usage(argv[0]);
        return 1;
    }
}
