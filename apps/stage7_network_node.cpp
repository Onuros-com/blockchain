#include "onuros/download_coordinator.hpp"
#include "onuros/header_sync.hpp"
#include "onuros/local_node.hpp"
#include "onuros/p2p_transport.hpp"
#include "onuros/stage7_relay.hpp"
#ifdef ONUROS_OPENSSL_ENABLED
#include "onuros/tls_transport.hpp"
#endif

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
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

bool send_all(PeerTransport& connection, const std::vector<std::uint8_t>& bytes) {
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

P2pFrame receive_frame(PeerTransport& connection, FrameStreamDecoder& stream) {
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

void send_frame(PeerTransport& connection, P2pMessageType type,
                std::uint64_t request_id,
                const std::vector<std::uint8_t>& payload) {
    const auto encoded = encode_p2p_frame(
        {stage7_protocol_version, type, request_id, payload});
    if (!send_all(connection, encoded))
        throw std::runtime_error("peer send failed");
}

void exchange_hello(PeerTransport& connection, FrameStreamDecoder& stream,
                    const Hash256& genesis, std::uint64_t local_nonce,
                    Height best_height, bool require_authenticated) {
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
    policy.require_authenticated_transport = require_authenticated;
    PeerSession session(policy);
    if (session.receive(remote_frame, connection.authenticated()) !=
            PeerSessionError::none ||
        session.state() != PeerSessionState::established)
        throw std::runtime_error("peer handshake rejected");
}

std::optional<TcpConnection> connect_with_retry(const std::string& address,
                                                std::uint16_t port) {
    for (unsigned attempt = 0U; attempt < 5000U; ++attempt) {
        auto connection = TcpConnection::connect_ipv4(address, port);
        if (connection) return connection;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

void serve_peer(PeerTransport& connection, const LocalNode& node,
                const Block& genesis, std::uint64_t nonce,
                bool require_authenticated) {
    FrameStreamDecoder stream({}, 2U * 1024U * 1024U);
    const auto* tip = node.store().index().active_tip();
    if (tip == nullptr) throw std::runtime_error("server tip is missing");
    exchange_hello(connection, stream, block_id(genesis.header), nonce,
                   tip->height, require_authenticated);

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

struct NetworkOptions {
    std::string role;
    std::filesystem::path data;
    std::uint16_t port = 0U;
    unsigned peers = 2U;
    std::uint64_t nonce = 20'000U;
    Height blocks = 3U;
    std::string bind = "127.0.0.1";
    std::string address = "127.0.0.1";
    std::filesystem::path certificate;
    std::filesystem::path private_key;
    std::filesystem::path ca;
    std::vector<std::string> expected_peers;
    std::filesystem::path manifest;
    std::string node_id;

    bool tls_enabled() const noexcept {
        return !certificate.empty() || !private_key.empty() || !ca.empty() ||
               !expected_peers.empty();
    }
};

void write_manifest(const NetworkOptions& options, const std::string& mode,
                    Height height, const Hash256& tip, const Hash256& genesis,
                    const Hash256& shielded_root,
                    const std::string& cipher, bool authenticated,
                    std::uint64_t useful_bytes = 0U) {
    if (options.manifest.empty()) return;
    if (options.node_id.empty())
        throw std::runtime_error("manifest requires node id");
    const auto parent = options.manifest.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    std::ofstream output(options.manifest);
    if (!output) throw std::runtime_error("manifest open failed");
    output << "role=" << options.role << '\n'
           << "node_id=" << options.node_id << '\n'
           << "mode=" << mode << '\n'
           << "height=" << height << '\n'
           << "genesis=" << hash_hex(genesis) << '\n'
           << "tip=" << hash_hex(tip) << '\n'
           << "shielded_root=" << hash_hex(shielded_root) << '\n'
           << "qualification_profile=transport-fixture-v1\n"
           << "active_privacy_protocol_qualified=false\n"
           << "useful_bytes=" << useful_bytes << '\n'
           << "transport_authenticated="
           << (authenticated ? "true" : "false") << '\n'
           << "tls_cipher=" << cipher << '\n'
           << "synchronization=PASS\n"
           << "process_exit_status=0\n"
           << "private_payloads_logged=false\n";
}

#ifdef ONUROS_OPENSSL_ENABLED
void complete_tls_handshake(TlsPeerTransport& transport) {
    for (unsigned attempt = 0U; attempt < 10'000U; ++attempt) {
        const auto status = transport.handshake();
        if (status == TlsStatus::ok) return;
        if (status == TlsStatus::error)
            throw std::runtime_error("TLS handshake failed");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("TLS handshake timeout");
}
#endif

std::optional<TcpConnection> accept_with_retry(TcpListener& listener) {
    for (unsigned attempt = 0U; attempt < 10'000U; ++attempt) {
        auto connection = listener.accept_one();
        if (connection) return connection;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

void run_server(const NetworkOptions& options) {
    LocalNode node(parameters(), test_pow);
    if (node.open(options.data).error != LocalNodeError::none)
        throw std::runtime_error("server database open failed");
    const auto genesis = ensure_genesis(node);
    ensure_relay_blocks(node, options.blocks);
#ifdef ONUROS_OPENSSL_ENABLED
    std::optional<TlsContext> tls_context;
    if (options.tls_enabled()) {
        tls_context = TlsContext::mutual(options.certificate.string(),
            options.private_key.string(), options.ca.string());
        if (!tls_context) throw std::runtime_error("server TLS material failed");
    }
#endif
    auto listener = TcpListener::listen_ipv4(options.bind, options.port);
    if (!listener) throw std::runtime_error("server listen failed");
    std::cout << "READY port=" << listener->port() << std::endl;
    std::string cipher = "none";
    for (unsigned peer = 0U; peer < options.peers; ++peer) {
        auto connection = accept_with_retry(*listener);
        if (!connection) throw std::runtime_error("server accept timeout");
        if (options.tls_enabled()) {
#ifdef ONUROS_OPENSSL_ENABLED
            TlsPeerTransport transport(*tls_context, std::move(*connection),
                TlsRole::server, options.expected_peers.at(peer));
            complete_tls_handshake(transport);
            cipher = transport.cipher() == nullptr ? "unknown" :
                                                     transport.cipher();
            serve_peer(transport, node, genesis, 10'000U + peer, true);
#else
            throw std::runtime_error("TLS support unavailable");
#endif
        } else {
            TcpPeerTransport transport(std::move(*connection));
            serve_peer(transport, node, genesis, 10'000U + peer, false);
        }
    }
    const auto* tip = node.store().index().active_tip();
    if (tip == nullptr) throw std::runtime_error("server tip is missing");
    const auto* tip_block = node.store().find(tip->id);
    if (tip_block == nullptr) throw std::runtime_error("server block is missing");
    write_manifest(options, "served", tip->height, tip->id,
                   block_id(genesis.header), tip_block->block.header.shielded_root,
                   cipher,
                   options.tls_enabled());
    std::cout << "SYNCED peers=" << options.peers
              << " tip=" << hash_hex(tip->id) << std::endl;
    std::cout << "stage7_sync=PASS role=server node_id=" << options.node_id
              << " height=" << tip->height
              << " tip=" << hash_hex(tip->id) << std::endl;
}

void run_client(const NetworkOptions& options) {
    LocalNode node(parameters(), test_pow);
    if (node.open(options.data).error != LocalNodeError::none)
        throw std::runtime_error("client database open failed");
    const auto genesis = ensure_genesis(node);
    const auto* existing = node.store().index().active_tip();
    if (existing != nullptr && existing->height == options.blocks) {
        const auto* recovered = node.store().find(existing->id);
        if (recovered == nullptr)
            throw std::runtime_error("recovered block is missing");
        write_manifest(options, "recovered", existing->height, existing->id,
                       block_id(genesis.header),
                       recovered->block.header.shielded_root,
                       "not_applicable", false);
        std::cout << "RECOVERED height=" << options.blocks
                  << " tip=" << hash_hex(existing->id)
                  << std::endl;
        std::cout << "stage7_sync=PASS role=client node_id="
                  << options.node_id << " mode=recovered height="
                  << existing->height << " tip=" << hash_hex(existing->id)
                  << std::endl;
        return;
    }
    auto connection = connect_with_retry(options.address, options.port);
    if (!connection) throw std::runtime_error("client connect timeout");
    std::string cipher = "none";
    std::optional<TcpPeerTransport> tcp_transport;
#ifdef ONUROS_OPENSSL_ENABLED
    std::optional<TlsContext> tls_context;
    std::optional<TlsPeerTransport> tls_transport;
#endif
    PeerTransport* transport = nullptr;
    if (options.tls_enabled()) {
#ifdef ONUROS_OPENSSL_ENABLED
        tls_context = TlsContext::mutual(options.certificate.string(),
            options.private_key.string(), options.ca.string());
        if (!tls_context) throw std::runtime_error("client TLS material failed");
        tls_transport.emplace(*tls_context, std::move(*connection),
                              TlsRole::client, options.expected_peers.front());
        complete_tls_handshake(*tls_transport);
        cipher = tls_transport->cipher() == nullptr ? "unknown" :
                                                     tls_transport->cipher();
        transport = &*tls_transport;
#else
        throw std::runtime_error("TLS support unavailable");
#endif
    } else {
        tcp_transport.emplace(std::move(*connection));
        transport = &*tcp_transport;
    }
    FrameStreamDecoder stream({}, 2U * 1024U * 1024U);
    exchange_hello(*transport, stream, block_id(genesis.header), options.nonce,
                   existing == nullptr ? 0U : existing->height,
                   options.tls_enabled());
    if (existing == nullptr) throw std::runtime_error("local genesis is missing");
    send_frame(*transport, P2pMessageType::get_headers, 2U,
               encode_header_request({{existing->id}, {}}));
    const auto headers_frame = receive_frame(*transport, stream);
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
        *headers, 100U + options.blocks * 60U);
    if (header_result.error != HeaderSyncError::none ||
        !header_result.stronger_tip ||
        header_chain.best_tip()->header.height != options.blocks)
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
        const auto compact_frame = receive_frame(*transport, stream);
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
        send_frame(*transport, P2pMessageType::get_block_transactions, request_id,
                   encode_missing_transaction_request(*request));
        CompactDownloadResult completed;
        while (!completed.block) {
            const auto chunk_frame = receive_frame(*transport, stream);
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
    if (tip == nullptr || tip->height != options.blocks)
        throw std::runtime_error("client did not activate synchronized block");
    const auto* tip_block = node.store().find(tip->id);
    if (tip_block == nullptr) throw std::runtime_error("client block is missing");
    write_manifest(options, "initial", tip->height, tip->id,
                   block_id(genesis.header), tip_block->block.header.shielded_root,
                   cipher,
                   options.tls_enabled(), coordinator.accounting().useful_bytes);
    std::cout << "SYNCED height=" << options.blocks
              << " useful_bytes=" << coordinator.accounting().useful_bytes
              << " tip=" << hash_hex(tip->id) << std::endl;
    std::cout << "stage7_sync=PASS role=client node_id=" << options.node_id
              << " mode=initial height=" << tip->height
              << " tip=" << hash_hex(tip->id) << std::endl;
}

void usage(const char* program) {
    std::cerr << "Usage: " << program
              << " --role server|client --data PATH --port PORT"
                 " [--peers COUNT] [--nonce VALUE] [--blocks HEIGHT]"
                 " [--bind ADDRESS|--address ADDRESS] [--manifest FILE]"
                 " [--node-id ID]"
                 " [--cert FILE --key FILE --ca FILE"
                 " --expected-peer DNS ...]\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        NetworkOptions options;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--role" && i + 1 < argc)
                options.role = argv[++i];
            else if (argument == "--data" && i + 1 < argc)
                options.data = argv[++i];
            else if (argument == "--port" && i + 1 < argc) {
                const auto value = std::stoul(argv[++i]);
                if (value == 0U || value > 65'535U)
                    throw std::invalid_argument("invalid port");
                options.port = static_cast<std::uint16_t>(value);
            } else if (argument == "--peers" && i + 1 < argc) {
                options.peers = static_cast<unsigned>(std::stoul(argv[++i]));
            } else if (argument == "--nonce" && i + 1 < argc) {
                options.nonce = std::stoull(argv[++i]);
            } else if (argument == "--blocks" && i + 1 < argc) {
                options.blocks = std::stoull(argv[++i]);
                if (options.blocks == 0U)
                    throw std::invalid_argument("invalid block count");
            } else if (argument == "--bind" && i + 1 < argc) {
                options.bind = argv[++i];
            } else if (argument == "--address" && i + 1 < argc) {
                options.address = argv[++i];
            } else if (argument == "--cert" && i + 1 < argc) {
                options.certificate = argv[++i];
            } else if (argument == "--key" && i + 1 < argc) {
                options.private_key = argv[++i];
            } else if (argument == "--ca" && i + 1 < argc) {
                options.ca = argv[++i];
            } else if (argument == "--expected-peer" && i + 1 < argc) {
                options.expected_peers.emplace_back(argv[++i]);
            } else if (argument == "--manifest" && i + 1 < argc) {
                options.manifest = argv[++i];
            } else if (argument == "--node-id" && i + 1 < argc) {
                options.node_id = argv[++i];
            } else {
                throw std::invalid_argument("unknown or incomplete argument");
            }
        }
        if (options.data.empty() || options.port == 0U ||
            (options.role != "server" && options.role != "client"))
            throw std::invalid_argument("required argument missing");
        if (options.peers == 0U || options.peers > 64U)
            throw std::invalid_argument("invalid peer count");
        if (options.tls_enabled()) {
            if (options.certificate.empty() || options.private_key.empty() ||
                options.ca.empty())
                throw std::invalid_argument("incomplete TLS configuration");
            const auto expected = options.role == "server" ? options.peers : 1U;
            if (options.expected_peers.size() != expected)
                throw std::invalid_argument("wrong expected-peer count");
        }
        const auto non_loopback = options.role == "server"
            ? options.bind != "127.0.0.1"
            : options.address != "127.0.0.1";
        if (non_loopback && !options.tls_enabled())
            throw std::invalid_argument(
                "non-loopback synchronization requires mutual TLS");
        if (!options.manifest.empty() && options.node_id.empty())
            throw std::invalid_argument("manifest requires node id");
        SocketRuntime runtime;
        if (!runtime.ready()) throw std::runtime_error("socket runtime failed");
        if (options.role == "server") run_server(options);
        else run_client(options);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        usage(argv[0]);
        return 1;
    }
}
