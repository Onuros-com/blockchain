#include "onuros/orchard_ffi_backend.hpp"
#include "onuros/private_node_commit.hpp"
#include "onuros/stage7_orchard_network.hpp"
#include "onuros/tls_transport.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace onuros;
using Clock = std::chrono::steady_clock;

constexpr std::array<std::uint8_t, 4> corpus_magic{'O', 'N', 'C', '1'};
constexpr std::array<std::uint8_t, 4> control_magic{'B', 'L', 'K', '1'};
constexpr std::size_t maximum_io_attempts = 3'600'000U;
constexpr auto io_retry_delay = std::chrono::milliseconds(1);
constexpr std::size_t maximum_chunk_bytes = 240U * 1024U;
constexpr std::uint64_t genesis_time = 1'700'000'000ULL;
constexpr std::uint64_t block_time = genesis_time + 60U;

Hash256 value(std::uint32_t number) {
    Hash256 result{};
    for (std::size_t index = 0U; index < 4U; ++index)
        result[result.size() - 1U - index] =
            static_cast<std::uint8_t>(number >> (8U * index));
    return result;
}

Hash256 chain_id() {
    const std::string name = "onuros-stage7-block-propagation-v1";
    return double_sha256(std::vector<std::uint8_t>(name.begin(), name.end()));
}

class CorpusReader {
    std::ifstream input_;
    std::uint64_t count_ = 0U;
    Hash256 anchor_{};
    std::uint64_t consumed_ = 0U;

    template <typename Integer>
    Integer read_little() {
        std::array<std::uint8_t, sizeof(Integer)> bytes{};
        input_.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        if (!input_) throw std::runtime_error("truncated corpus header");
        Integer result = 0;
        for (std::size_t index = 0U; index < bytes.size(); ++index)
            result |= static_cast<Integer>(bytes[index]) << (8U * index);
        return result;
    }

public:
    explicit CorpusReader(const std::filesystem::path& path)
        : input_(path, std::ios::binary) {
        std::array<std::uint8_t, 4> magic{};
        input_.read(reinterpret_cast<char*>(magic.data()), magic.size());
        if (!input_ || magic != corpus_magic)
            throw std::runtime_error("invalid Orchard corpus magic");
        count_ = read_little<std::uint64_t>();
        input_.read(reinterpret_cast<char*>(anchor_.data()), anchor_.size());
        if (!input_ || count_ == 0U)
            throw std::runtime_error("invalid Orchard corpus header");
    }

    std::uint64_t count() const noexcept { return count_; }
    const Hash256& anchor() const noexcept { return anchor_; }

    std::optional<TransactionEnvelope> read() {
        if (consumed_ == count_) return std::nullopt;
        const auto length = read_little<std::uint32_t>();
        if (length == 0U || length > 16U * 1024U)
            throw std::runtime_error("invalid block corpus transaction length");
        std::vector<std::uint8_t> body(length);
        input_.read(reinterpret_cast<char*>(body.data()), body.size());
        if (!input_) throw std::runtime_error("truncated corpus transaction");
        ++consumed_;
        return TransactionEnvelope{private_transaction_envelope_version,
                                   std::move(body)};
    }
};

class FramedTls {
    TlsPeerTransport transport_;
    FrameStreamDecoder decoder_{P2pFrameLimits{}, 2U * 1024U * 1024U};

public:
    FramedTls(TlsContext& context, TcpConnection connection, TlsRole role,
              const std::string& expected_peer)
        : transport_(context, std::move(connection), role, expected_peer) {}

    void handshake_tls() {
        for (std::size_t attempt = 0U; attempt < maximum_io_attempts; ++attempt) {
            const auto status = transport_.handshake();
            if (status == TlsStatus::ok) return;
            if (status == TlsStatus::error)
                throw std::runtime_error("TLS handshake failed");
            std::this_thread::sleep_for(io_retry_delay);
        }
        throw std::runtime_error("TLS handshake timeout");
    }

    std::size_t send(const P2pFrame& frame) {
        const auto encoded = encode_p2p_frame(frame);
        std::size_t offset = 0U;
        for (std::size_t attempt = 0U;
             attempt < maximum_io_attempts && offset < encoded.size(); ++attempt) {
            const auto sent = transport_.send_some(
                encoded.data() + offset, encoded.size() - offset);
            if (sent.status == SocketIoStatus::ok) offset += sent.bytes;
            else if (sent.status != SocketIoStatus::would_block)
                throw std::runtime_error("TLS peer write failed");
            if (offset < encoded.size()) std::this_thread::sleep_for(io_retry_delay);
        }
        if (offset != encoded.size())
            throw std::runtime_error("TLS peer write timeout");
        return encoded.size();
    }

    P2pFrame receive() {
        std::array<std::uint8_t, 64U * 1024U> bytes{};
        for (std::size_t attempt = 0U; attempt < maximum_io_attempts; ++attempt) {
            const auto parsed = decoder_.next();
            if (parsed.status == FrameStreamStatus::frame_ready)
                return parsed.frame;
            if (parsed.status == FrameStreamStatus::failed)
                throw std::runtime_error("invalid TLS peer frame");
            const auto received = transport_.receive_some(bytes.data(), bytes.size());
            if (received.status == SocketIoStatus::ok) {
                if (!decoder_.feed(bytes.data(), received.bytes))
                    throw std::runtime_error("TLS frame buffer limit exceeded");
            } else if (received.status != SocketIoStatus::would_block) {
                throw std::runtime_error("TLS peer disconnected");
            }
            std::this_thread::sleep_for(io_retry_delay);
        }
        throw std::runtime_error("TLS peer read timeout");
    }

    const char* cipher() const noexcept { return transport_.cipher(); }
};

TcpConnection accept_connection(TcpListener& listener) {
    for (std::size_t attempt = 0U; attempt < maximum_io_attempts; ++attempt) {
        auto connection = listener.accept_one();
        if (connection) return std::move(*connection);
        std::this_thread::sleep_for(io_retry_delay);
    }
    throw std::runtime_error("peer accept timeout");
}

TcpConnection connect_connection(const std::string& address,
                                 std::uint16_t port) {
    for (std::size_t attempt = 0U; attempt < maximum_io_attempts; ++attempt) {
        auto connection = TcpConnection::connect_ipv4(address, port);
        if (connection) return std::move(*connection);
        std::this_thread::sleep_for(io_retry_delay);
    }
    throw std::runtime_error("peer connect timeout");
}

void exchange_hello(FramedTls& peer, std::uint64_t nonce) {
    HelloMessage local;
    local.chain_id = chain_id();
    local.genesis_hash = value(1U);
    local.services = p2p_service_full_node | p2p_service_compact_relay;
    local.node_nonce = nonce;
    peer.send({stage7_protocol_version, P2pMessageType::hello, 1U,
               encode_hello(local)});
    const auto remote = peer.receive();
    HandshakePolicy policy;
    policy.chain_id = local.chain_id;
    policy.genesis_hash = local.genesis_hash;
    policy.local_nonce = nonce;
    policy.required_services = p2p_service_full_node |
                               p2p_service_compact_relay;
    policy.require_authenticated_transport = true;
    PeerSession session(policy);
    if (session.receive(remote, true) != PeerSessionError::none ||
        session.state() != PeerSessionState::established)
        throw std::runtime_error("P2P handshake rejected");
}

Hash256 pow_hash(const BlockHeader& header) {
    return double_sha256(encode_block_header(header));
}

Target256 pow_limit() {
    const auto result = decode_compact_target(0x207fffffU);
    if (!result) throw std::runtime_error("proof-of-work limit decode failed");
    return *result;
}

void solve(Block& block) {
    const auto limit = pow_limit();
    while (!hash_meets_compact_target(pow_hash(block.header),
                                      block.header.compact_target, limit)) {
        if (block.header.nonce == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("proof-of-work nonce exhausted");
        ++block.header.nonce;
    }
}

Block make_genesis(const Hash256& anchor) {
    Block block;
    block.header.version = 2U;
    block.header.height = 0U;
    block.header.shielded_root = anchor;
    block.header.timestamp = genesis_time;
    block.header.compact_target = 0x207fffffU;
    block.transactions = {{private_transaction_envelope_version, {}}};
    block.header.transactions_root = transaction_root(block.transactions);
    solve(block);
    return block;
}

PrivateBundleLimits bundle_limits() {
    return {16U * 1024U, 4U, 16U * 1024U};
}

PrivateAdmissionLimits admission_limits() {
    return {16U * 1024U, 4U, 4U, 4096U};
}

BlockChunkLimits chunk_limits() {
    BlockChunkLimits limits;
    limits.maximum_chunk_bytes = maximum_chunk_bytes;
    limits.maximum_transactions_per_chunk = 64U;
    limits.maximum_transaction_body_bytes = 16U * 1024U;
    limits.maximum_total_chunks = 128U;
    limits.maximum_total_transactions = 4096U;
    limits.maximum_total_transfer_bytes =
        static_cast<std::uint32_t>(max_serialized_block_bytes);
    return limits;
}

LocalNodeParameters node_parameters() {
    LocalNodeParameters result;
    result.validation_limits =
        {2U, private_transaction_envelope_version, max_serialized_block_bytes,
         4096U, 16U * 1024U};
    result.decode_limits =
        {max_serialized_block_bytes, 4096U, 16U * 1024U};
    result.difficulty.target_block_seconds = 60U;
    result.difficulty.retarget_interval = 60U;
    result.difficulty.adjustment_clamp_factor = 4U;
    result.difficulty.proof_of_work_limit = pow_limit();
    result.max_future_seconds = 120U;
    result.max_database_bytes = 64U * 1024U * 1024U;
    return result;
}

struct BuiltBlock {
    Hash256 anchor{};
    Block genesis;
    Block block;
};

BuiltBlock build_block(const std::filesystem::path& corpus_path,
                       std::size_t workers) {
    CorpusReader corpus(corpus_path);
    if (corpus.count() < 2800U)
        throw std::runtime_error("block corpus requires at least 2800 entries");
    const auto anchor = corpus.anchor();
    const auto genesis = make_genesis(anchor);
    std::vector<TransactionEnvelope> private_transactions;
    std::size_t encoded_bytes = block_prefix_encoded_size + 136U;
    while (const auto transaction = corpus.read()) {
        const auto transaction_bytes = encode_transaction(*transaction).size();
        if (transaction_bytes > max_serialized_block_bytes - encoded_bytes)
            break;
        encoded_bytes += transaction_bytes;
        private_transactions.push_back(*transaction);
    }
    if (encoded_bytes < max_serialized_block_bytes * 99U / 100U)
        throw std::runtime_error("corpus does not fill the 99 percent block window");

    ShieldedState state(block_id(genesis.header), anchor);
    OrchardFfiBackend backend;
    CanonicalPrivateTransactionVerifier verifier(backend, bundle_limits());
    const auto admission = PrivateBlockAdmission::prepare_parallel(
        state, state.tip(), private_transactions, verifier, admission_limits(),
        workers);
    if (!admission.accepted())
        throw std::runtime_error("sender Orchard block preparation failed");
    OrchardFfiRootCalculator roots;
    const auto root = roots.calculate({}, admission.prepared->commitments());
    if (!root) throw std::runtime_error("sender Orchard root failed");
    OnurosRewardPolicy economics;
    const auto amounts = economics.allocate(1U, admission.prepared->fees(), 0);
    const auto team = value(10U);
    const auto ecosystem = value(11U);
    const auto miner = value(12U);
    const auto reward = make_private_reward_transaction(
        {amounts, amounts.miner == 0 ? Hash256{} : miner,
         amounts.team == 0 ? Hash256{} : team,
         amounts.ecosystem == 0 ? Hash256{} : ecosystem});

    Block block;
    block.header.version = 2U;
    block.header.height = 1U;
    block.header.previous = block_id(genesis.header);
    block.header.shielded_root = *root;
    block.header.timestamp = block_time;
    block.header.compact_target = 0x207fffffU;
    block.transactions.reserve(private_transactions.size() + 1U);
    block.transactions.push_back(reward);
    block.transactions.insert(block.transactions.end(),
                              private_transactions.begin(),
                              private_transactions.end());
    block.header.transactions_root = transaction_root(block.transactions);
    solve(block);
    const auto final_bytes = encode_block(block).size();
    if (final_bytes < max_serialized_block_bytes * 99U / 100U ||
        final_bytes > max_serialized_block_bytes)
        throw std::runtime_error("built block is outside consensus size window");
    return {anchor, genesis, std::move(block)};
}

std::vector<std::uint8_t> encode_control(
        const Hash256& anchor, std::uint32_t overlap) {
    std::vector<std::uint8_t> output(control_magic.begin(), control_magic.end());
    output.insert(output.end(), anchor.begin(), anchor.end());
    detail::append_little_endian(output, overlap);
    return output;
}

std::pair<Hash256, std::uint32_t> decode_control(
        const std::vector<std::uint8_t>& input) {
    if (input.size() != 40U ||
        !std::equal(control_magic.begin(), control_magic.end(), input.begin()))
        throw std::runtime_error("invalid block control");
    Hash256 anchor{};
    std::copy(input.begin() + 4, input.begin() + 36, anchor.begin());
    detail::ByteReader reader(input);
    std::array<std::uint8_t, 4> ignored{};
    for (auto& byte : ignored)
        if (!reader.read_little_endian(byte))
            throw std::runtime_error("truncated block control");
    Hash256 ignored_anchor{};
    std::uint32_t overlap = 0U;
    if (!reader.read_hash(ignored_anchor) ||
        !reader.read_little_endian(overlap) || !reader.exhausted() ||
        overlap > 100U)
        throw std::runtime_error("invalid block overlap");
    return {anchor, overlap};
}

struct Options {
    std::string role;
    std::string bind = "0.0.0.0";
    std::string address;
    std::uint16_t port = 0U;
    std::filesystem::path certificate;
    std::filesystem::path private_key;
    std::filesystem::path ca;
    std::string expected_peer;
    std::filesystem::path corpus;
    std::filesystem::path data_dir;
    std::filesystem::path manifest;
    std::string receiver_id;
    std::uint32_t overlap = 50U;
    std::size_t workers = 4U;
};

std::string argument(int argc, char** argv, const std::string& name,
                     const std::string& fallback = {}) {
    for (int index = 1; index + 1 < argc; ++index)
        if (argv[index] == name) return argv[index + 1];
    return fallback;
}

std::uint64_t number_argument(int argc, char** argv, const std::string& name,
                              std::uint64_t fallback) {
    const auto text = argument(argc, argv, name);
    if (text.empty()) return fallback;
    std::size_t parsed = 0U;
    const auto value = std::stoull(text, &parsed);
    if (parsed != text.size())
        throw std::runtime_error("invalid numeric argument: " + name);
    return value;
}

Options parse_options(int argc, char** argv) {
    Options options;
    options.role = argument(argc, argv, "--role");
    options.bind = argument(argc, argv, "--bind", options.bind);
    options.address = argument(argc, argv, "--address");
    const auto port = number_argument(argc, argv, "--port", 0U);
    if (port == 0U || port > 65'535U)
        throw std::runtime_error("invalid port");
    options.port = static_cast<std::uint16_t>(port);
    options.certificate = argument(argc, argv, "--cert");
    options.private_key = argument(argc, argv, "--key");
    options.ca = argument(argc, argv, "--ca");
    options.expected_peer = argument(argc, argv, "--expected-peer");
    options.corpus = argument(argc, argv, "--corpus");
    options.data_dir = argument(argc, argv, "--data-dir");
    options.manifest = argument(argc, argv, "--manifest");
    options.receiver_id = argument(argc, argv, "--receiver-id");
    options.overlap = static_cast<std::uint32_t>(
        number_argument(argc, argv, "--overlap", options.overlap));
    options.workers = static_cast<std::size_t>(
        number_argument(argc, argv, "--workers", options.workers));
    if (options.role.empty() || options.certificate.empty() ||
        options.private_key.empty() || options.ca.empty() ||
        options.expected_peer.empty() || options.manifest.empty() ||
        options.overlap > 100U || options.workers == 0U ||
        options.workers > 256U)
        throw std::runtime_error("missing or out-of-range block option");
    return options;
}

void send_prefill(FramedTls& peer, const Block& block,
                  std::size_t overlap_count, std::size_t& wire_bytes) {
    std::uint64_t request = 100U;
    for (std::size_t offset = 1U; offset <= overlap_count; offset += 16U) {
        const auto end = std::min(overlap_count + 1U, offset + 16U);
        const std::vector<TransactionEnvelope> batch(
            block.transactions.begin() + static_cast<std::ptrdiff_t>(offset),
            block.transactions.begin() + static_cast<std::ptrdiff_t>(end));
        wire_bytes += peer.send({stage7_protocol_version,
            P2pMessageType::transactions, request++,
            encode_network_transactions(batch)});
    }
}

void run_sender(const Options& options) {
    if (options.corpus.empty())
        throw std::runtime_error("sender corpus is required");
    const auto built = build_block(options.corpus, options.workers);
    auto context = TlsContext::mutual(options.certificate.string(),
        options.private_key.string(), options.ca.string());
    if (!context) throw std::runtime_error("sender TLS material failed");
    auto listener = TcpListener::listen_ipv4(options.bind, options.port, 4);
    if (!listener) throw std::runtime_error("sender listen failed");
    std::cout << "READY role=sender port=" << listener->port()
              << " block_bytes=" << encode_block(built.block).size()
              << " block_id=" << hash_hex(block_id(built.block.header))
              << std::endl;
    FramedTls receiver(*context, accept_connection(*listener), TlsRole::server,
                       options.expected_peer);
    receiver.handshake_tls();
    exchange_hello(receiver, 40'001U);
    std::size_t wire_bytes = receiver.send({stage7_protocol_version,
        P2pMessageType::ping, 2U, encode_control(built.anchor, options.overlap)});
    const auto private_count = built.block.transactions.size() - 1U;
    const auto overlap_count = private_count * options.overlap / 100U;
    send_prefill(receiver, built.block, overlap_count, wire_bytes);
    wire_bytes += receiver.send({stage7_protocol_version,
        P2pMessageType::compact_block, 3U,
        encode_compact_block_announcement(
            make_compact_block_announcement(built.block))});
    const auto request_frame = receiver.receive();
    if (request_frame.type != P2pMessageType::get_block_transactions)
        throw std::runtime_error("sender expected missing transaction request");
    const auto request = decode_missing_transaction_request(
        request_frame.payload, 4096U);
    if (!request || request->block_identifier != block_id(built.block.header))
        throw std::runtime_error("sender rejected missing transaction request");
    const auto chunks = make_block_transaction_chunks(
        built.block, request->indexes, maximum_chunk_bytes);
    if (!chunks) throw std::runtime_error("sender block chunking failed");
    for (const auto& chunk : *chunks)
        wire_bytes += receiver.send({stage7_protocol_version,
            P2pMessageType::block_transactions, 3U,
            encode_block_transaction_chunk(chunk)});
    const auto result = receiver.receive();
    const auto identifier = block_id(built.block.header);
    if (result.type != P2pMessageType::pong || result.request_id != 3U ||
        result.payload != std::vector<std::uint8_t>(
            identifier.begin(), identifier.end()))
        throw std::runtime_error("receiver completion mismatch");
    std::ofstream manifest(options.manifest);
    if (!manifest) throw std::runtime_error("sender manifest open failed");
    manifest << "role=sender\n"
             << "block_bytes=" << encode_block(built.block).size() << '\n'
             << "transactions=" << built.block.transactions.size() << '\n'
             << "mempool_overlap_percent=" << options.overlap << '\n'
             << "mempool_overlap_transactions=" << overlap_count << '\n'
             << "wire_bytes_sent=" << wire_bytes << '\n'
             << "block_id=" << hash_hex(identifier) << '\n'
             << "process_exit_status=0\nprivate_payloads_logged=false\n";
    std::cout << "stage7_block_sender=PASS block_id="
              << hash_hex(identifier) << std::endl;
}

struct ReceiverResult {
    std::size_t block_bytes = 0U;
    std::size_t transactions = 0U;
    std::size_t overlap_transactions = 0U;
    std::size_t announcement_bytes = 0U;
    std::size_t request_bytes = 0U;
    std::size_t response_bytes = 0U;
    double elapsed = 0.0;
    Hash256 block_identifier{};
    std::string cipher;
};

ReceiverResult receive_block(FramedTls& sender, const Options& options,
                             const Hash256& anchor, std::uint32_t overlap) {
    const auto genesis = make_genesis(anchor);
    ShieldedState admission_state(block_id(genesis.header), anchor);
    OrchardNetworkAdmission admission(
        admission_state, bundle_limits(),
        {4096U, 32U * 1024U * 1024U, 16'384U, 16U * 1024U, 4U},
        4096U, 32U * 1024U * 1024U);
    ValidatedRelayPool available(4096U, 32U * 1024U * 1024U);
    std::size_t overlap_transactions = 0U;
    P2pFrame announcement_frame;
    for (;;) {
        auto frame = sender.receive();
        if (frame.type == P2pMessageType::compact_block) {
            announcement_frame = std::move(frame);
            break;
        }
        if (frame.type != P2pMessageType::transactions)
            throw std::runtime_error("receiver expected prefill transaction batch");
        const auto decoded = decode_network_transactions(
            frame.payload, {256U * 1024U, 32U, 16U * 1024U});
        const auto admitted = admission.handle_frame_parallel(frame, options.workers);
        if (!decoded.accepted() || !admitted || !admitted->accepted() ||
            admitted->accepted_transactions != decoded.transactions.size())
            throw std::runtime_error("receiver prefill admission failed");
        for (const auto& transaction : decoded.transactions) {
            if (!available.remember_validated(transaction))
                throw std::runtime_error("receiver duplicate prefill transaction");
            ++overlap_transactions;
        }
    }
    const auto started = Clock::now();
    const auto announcement = decode_compact_block_announcement(
        announcement_frame.payload, 4096U, 256U * 1024U);
    if (!announcement || announcement->header.previous != block_id(genesis.header))
        throw std::runtime_error("receiver compact announcement failed");
    CompactBlockDownload download(*announcement, available, chunk_limits(),
        [](const TransactionEnvelope& transaction) {
            return transaction.version == private_transaction_envelope_version &&
                   transaction.body.size() <= 16U * 1024U;
        });
    CompactDownloadResult completed;
    const auto request = download.start(completed);
    if (!request || completed.error != CompactDownloadError::none)
        throw std::runtime_error("receiver missing transaction request failed");
    const auto request_payload = encode_missing_transaction_request(*request);
    const auto request_wire = sender.send({stage7_protocol_version,
        P2pMessageType::get_block_transactions, 3U, request_payload});
    std::size_t response_wire = 0U;
    while (!completed.block) {
        const auto frame = sender.receive();
        if (frame.type != P2pMessageType::block_transactions)
            throw std::runtime_error("receiver expected block transaction chunk");
        response_wire += p2p_frame_header_size + frame.payload.size();
        const auto chunk = decode_block_transaction_chunk(frame.payload,
                                                           chunk_limits());
        if (!chunk) throw std::runtime_error("receiver chunk decode failed");
        completed = download.add_chunk(*chunk);
        if (completed.error != CompactDownloadError::none)
            throw std::runtime_error("receiver compact reconstruction failed");
    }
    const auto& block = *completed.block;
    const auto encoded = encode_block(block);
    if (encoded.size() < max_serialized_block_bytes * 99U / 100U ||
        encoded.size() > max_serialized_block_bytes)
        throw std::runtime_error("receiver block outside size window");

    std::filesystem::create_directories(options.data_dir);
    const auto blocks = options.data_dir / "blocks.db";
    const auto shielded_path = options.data_dir / "shielded.db";
    const auto journal = options.data_dir / "private-commit.db";
    if (std::filesystem::exists(blocks) ||
        std::filesystem::exists(shielded_path) ||
        std::filesystem::exists(journal))
        throw std::runtime_error("receiver data directory is not fresh");
    const auto parameters = node_parameters();
    OrchardFfiBackend backend;
    CanonicalPrivateTransactionVerifier verifier(backend, bundle_limits());
    OrchardFfiRootCalculator roots;
    PrivateRewardPolicy rewards(value(10U), value(11U));
    {
        LocalNode node(parameters, [](const BlockHeader& header) {
            return std::optional<Hash256>{pow_hash(header)};
        });
        if (node.open(blocks).error != LocalNodeError::none ||
            node.submit(genesis, genesis_time).error != LocalNodeError::none)
            throw std::runtime_error("receiver genesis activation failed");
        PersistentShieldedState shielded(
            block_id(genesis.header), anchor, 64U * 1024U * 1024U, 16'384U);
        if (shielded.open(shielded_path) != ShieldedStoreError::none)
            throw std::runtime_error("receiver shielded state open failed");
        PrivateNodeCommitCoordinator coordinator(
            node, shielded, verifier, roots, rewards, admission_limits(), journal,
            parameters.decode_limits, 32U * 1024U * 1024U);
        if (!coordinator.submit_parallel(block, block_time,
                                         options.workers).accepted())
            throw std::runtime_error("receiver durable block activation failed");
    }
    {
        LocalNode restarted(parameters, [](const BlockHeader& header) {
            return std::optional<Hash256>{pow_hash(header)};
        });
        if (restarted.open(blocks).error != LocalNodeError::none ||
            restarted.active_state().tip() != block_id(block.header))
            throw std::runtime_error("receiver block restart recovery failed");
        PersistentShieldedState shielded(
            block_id(genesis.header), anchor, 64U * 1024U * 1024U, 16'384U);
        if (shielded.open(shielded_path) != ShieldedStoreError::none ||
            shielded.state().tip() != block_id(block.header) ||
            shielded.state().root() != block.header.shielded_root ||
            shielded.state().spent_count() != block.transactions.size() - 1U ||
            shielded.state().commitment_count() != block.transactions.size() - 1U)
            throw std::runtime_error("receiver shielded restart recovery failed");
    }
    const auto elapsed = std::chrono::duration<double>(
        Clock::now() - started).count();
    if (overlap_transactions !=
        (block.transactions.size() - 1U) * overlap / 100U)
        throw std::runtime_error("receiver mempool overlap mismatch");
    return {encoded.size(), block.transactions.size(), overlap_transactions,
            announcement_frame.payload.size() + p2p_frame_header_size,
            request_wire, response_wire, elapsed, block_id(block.header),
            sender.cipher() == nullptr ? "unknown" : sender.cipher()};
}

void run_receiver(const Options& options) {
    if (options.address.empty() || options.data_dir.empty() ||
        options.receiver_id.empty())
        throw std::runtime_error("receiver connection/data options missing");
    auto context = TlsContext::mutual(options.certificate.string(),
        options.private_key.string(), options.ca.string());
    if (!context) throw std::runtime_error("receiver TLS material failed");
    FramedTls sender(*context, connect_connection(options.address, options.port),
                     TlsRole::client, options.expected_peer);
    sender.handshake_tls();
    exchange_hello(sender, 40'002U);
    const auto control_frame = sender.receive();
    if (control_frame.type != P2pMessageType::ping)
        throw std::runtime_error("receiver expected block control");
    const auto [anchor, overlap] = decode_control(control_frame.payload);
    const auto result = receive_block(sender, options, anchor, overlap);
    sender.send({stage7_protocol_version, P2pMessageType::pong, 3U,
        std::vector<std::uint8_t>(result.block_identifier.begin(),
                                  result.block_identifier.end())});
    std::ofstream manifest(options.manifest);
    if (!manifest) throw std::runtime_error("receiver manifest open failed");
    manifest << "role=receiver\n"
             << "receiver_id=" << options.receiver_id << '\n'
             << "block_bytes=" << result.block_bytes << '\n'
             << "transactions=" << result.transactions << '\n'
             << "mempool_overlap_percent=" << overlap << '\n'
             << "mempool_overlap_transactions="
             << result.overlap_transactions << '\n'
             << "announcement_bytes=" << result.announcement_bytes << '\n'
             << "request_bytes=" << result.request_bytes << '\n'
             << "response_bytes=" << result.response_bytes << '\n'
             << std::fixed << std::setprecision(6)
             << "propagation_validation_seconds=" << result.elapsed << '\n'
             << "block_id=" << hash_hex(result.block_identifier) << '\n'
             << "limits_exceeded=0\nvalidation=PASS\n"
             << "durable_activation=PASS\nrestart_recovery=PASS\n"
             << "verification_backend=orchard-ffi\nprocess_exit_status=0\n"
             << "private_payloads_logged=false\n"
             << "tls_cipher=" << result.cipher << '\n';
    std::cout << "stage7_block_receiver=PASS receiver_id="
              << options.receiver_id << " block_bytes=" << result.block_bytes
              << " propagation_validation_seconds=" << std::fixed
              << std::setprecision(6) << result.elapsed
              << " block_id=" << hash_hex(result.block_identifier) << std::endl;
}

void usage(const char* program) {
    std::cerr << "Usage: " << program
              << " --role sender|receiver --port PORT --cert FILE --key FILE"
                 " --ca FILE --expected-peer DNS --manifest FILE [options]\n"
              << "  sender: --bind ADDRESS --corpus FILE [--overlap 50]\n"
              << "  receiver: --address HOST --data-dir DIR"
                 " --receiver-id ID [--workers 4]\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        SocketRuntime sockets;
        if (!sockets.ready()) throw std::runtime_error("socket runtime unavailable");
        const auto options = parse_options(argc, argv);
        if (options.role == "sender") run_sender(options);
        else if (options.role == "receiver") run_receiver(options);
        else {
            usage(argv[0]);
            return 2;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "stage7_block_propagation=FAIL reason="
                  << error.what() << '\n';
        return 1;
    }
}
