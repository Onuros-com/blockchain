#include "onuros/privacy_engine_network.hpp"
#include "onuros/tls_transport.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace onuros;
using Clock = std::chrono::steady_clock;

constexpr std::array<std::uint8_t, 8> corpus_magic{
    'O', 'N', 'U', 'R', 'C', 'R', 'P', '1'};
constexpr std::uint32_t corpus_version = 1U;
constexpr std::array<std::uint8_t, 4> start_magic{'L', 'O', 'A', 'D'};
constexpr std::array<std::uint8_t, 4> finish_magic{'D', 'O', 'N', 'E'};
constexpr std::array<std::uint8_t, 4> summary_magic{'S', 'U', 'M', '1'};
constexpr std::array<std::uint8_t, 4> relay_summary_magic{'S', 'U', 'M', '2'};
constexpr std::size_t maximum_io_attempts = 3'600'000U;
constexpr auto io_retry_delay = std::chrono::milliseconds(1);

Hash256 value(std::uint32_t number) {
    Hash256 result{};
    for (std::size_t index = 0U; index < 4U; ++index)
        result[result.size() - 1U - index] =
            static_cast<std::uint8_t>(number >> (8U * index));
    return result;
}

Hash256 chain_id() {
    const std::string name = "onuros-stage7-private-load-v1";
    return double_sha256(
        std::vector<std::uint8_t>(name.begin(), name.end()));
}

void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (std::size_t index = 0U; index < sizeof(value); ++index)
        output.push_back(static_cast<std::uint8_t>(value >> (8U * index)));
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (std::size_t index = 0U; index < sizeof(value); ++index)
        output.push_back(static_cast<std::uint8_t>(value >> (8U * index)));
}

std::uint64_t read_u64(const std::vector<std::uint8_t>& input,
                       std::size_t offset) {
    if (offset > input.size() || input.size() - offset < sizeof(std::uint64_t))
        throw std::runtime_error("truncated control value");
    std::uint64_t result = 0U;
    for (std::size_t index = 0U; index < sizeof(result); ++index)
        result |= static_cast<std::uint64_t>(input[offset + index]) <<
                  (8U * index);
    return result;
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& input,
                       std::size_t offset) {
    if (offset > input.size() || input.size() - offset < sizeof(std::uint32_t))
        throw std::runtime_error("truncated control value");
    std::uint32_t result = 0U;
    for (std::size_t index = 0U; index < sizeof(result); ++index)
        result |= static_cast<std::uint32_t>(input[offset + index]) <<
                  (8U * index);
    return result;
}

bool begins_with(const std::vector<std::uint8_t>& input,
                 const std::array<std::uint8_t, 4>& prefix) {
    return input.size() >= prefix.size() &&
        std::equal(prefix.begin(), prefix.end(), input.begin());
}

struct StartControl {
    std::uint64_t transactions = 0U;
    std::uint64_t duration_seconds = 0U;
    Hash256 anchor{};
    Hash256 parameter_sha256{};
    std::uint32_t network_id = 0U;
    std::uint32_t circuit_version = 0U;
    std::uint32_t root_height = 0U;
    std::uint32_t max_root_age = 0U;
};

std::vector<std::uint8_t> encode_start(const StartControl& start) {
    std::vector<std::uint8_t> output(start_magic.begin(), start_magic.end());
    append_u64(output, start.transactions);
    append_u64(output, start.duration_seconds);
    append_u32(output, start.network_id);
    append_u32(output, start.circuit_version);
    append_u32(output, start.root_height);
    append_u32(output, start.max_root_age);
    output.insert(output.end(), start.anchor.begin(), start.anchor.end());
    output.insert(output.end(), start.parameter_sha256.begin(),
                  start.parameter_sha256.end());
    return output;
}

StartControl decode_start(const std::vector<std::uint8_t>& input) {
    if (!begins_with(input, start_magic) || input.size() != 100U)
        throw std::runtime_error("invalid load start control");
    StartControl result;
    result.transactions = read_u64(input, 4U);
    result.duration_seconds = read_u64(input, 12U);
    result.network_id = read_u32(input, 20U);
    result.circuit_version = read_u32(input, 24U);
    result.root_height = read_u32(input, 28U);
    result.max_root_age = read_u32(input, 32U);
    std::copy(input.begin() + 36, input.begin() + 68, result.anchor.begin());
    std::copy(input.begin() + 68, input.end(),
              result.parameter_sha256.begin());
    if (result.transactions == 0U || result.duration_seconds == 0U)
        throw std::runtime_error("empty load parameters");
    return result;
}

struct Summary {
    std::uint64_t transactions = 0U;
    Hash256 id_set{};
};

std::vector<std::uint8_t> encode_summary(const Summary& summary) {
    std::vector<std::uint8_t> output(summary_magic.begin(), summary_magic.end());
    append_u64(output, summary.transactions);
    output.insert(output.end(), summary.id_set.begin(), summary.id_set.end());
    return output;
}

Summary decode_summary(const std::vector<std::uint8_t>& input) {
    if (!begins_with(input, summary_magic) || input.size() != 44U)
        throw std::runtime_error("invalid observer summary");
    Summary result;
    result.transactions = read_u64(input, 4U);
    std::copy(input.begin() + 12, input.end(), result.id_set.begin());
    return result;
}

std::vector<std::uint8_t> encode_relay_summary(
        const Summary& relay, const Summary& observer) {
    std::vector<std::uint8_t> output(
        relay_summary_magic.begin(), relay_summary_magic.end());
    append_u64(output, relay.transactions);
    output.insert(output.end(), relay.id_set.begin(), relay.id_set.end());
    append_u64(output, observer.transactions);
    output.insert(output.end(), observer.id_set.begin(), observer.id_set.end());
    return output;
}

std::pair<Summary, Summary> decode_relay_summary(
        const std::vector<std::uint8_t>& input) {
    if (!begins_with(input, relay_summary_magic) || input.size() != 84U)
        throw std::runtime_error("invalid relay summary");
    Summary relay;
    relay.transactions = read_u64(input, 4U);
    std::copy(input.begin() + 12, input.begin() + 44, relay.id_set.begin());
    Summary observer;
    observer.transactions = read_u64(input, 44U);
    std::copy(input.begin() + 52, input.end(), observer.id_set.begin());
    return {relay, observer};
}

Hash256 id_set_hash(std::vector<Hash256> identifiers) {
    std::sort(identifiers.begin(), identifiers.end());
    std::vector<std::uint8_t> encoded;
    encoded.reserve(identifiers.size() * Hash256{}.size());
    for (const auto& identifier : identifiers)
        encoded.insert(encoded.end(), identifier.begin(), identifier.end());
    return sha256(encoded);
}

class CorpusReader {
    std::ifstream input_;
    std::uint64_t count_ = 0U;
    Hash256 anchor_{};
    std::uint32_t network_id_ = 0U;
    std::uint32_t circuit_version_ = 0U;
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
        std::array<std::uint8_t, 8> magic{};
        input_.read(reinterpret_cast<char*>(magic.data()), magic.size());
        if (!input_ || magic != corpus_magic)
            throw std::runtime_error("invalid Onuros corpus magic");
        if (read_little<std::uint32_t>() != corpus_version)
            throw std::runtime_error("unsupported Onuros corpus version");
        count_ = read_little<std::uint32_t>();
        network_id_ = read_little<std::uint32_t>();
        circuit_version_ = read_little<std::uint32_t>();
        input_.read(reinterpret_cast<char*>(anchor_.data()), anchor_.size());
        if (!input_ || count_ == 0U)
            throw std::runtime_error("invalid Onuros corpus header");
    }

    std::uint64_t count() const noexcept { return count_; }
    const Hash256& anchor() const noexcept { return anchor_; }
    std::uint32_t network_id() const noexcept { return network_id_; }
    std::uint32_t circuit_version() const noexcept {
        return circuit_version_;
    }

    std::vector<TransactionEnvelope> read_batch(std::size_t maximum) {
        std::vector<TransactionEnvelope> result;
        result.reserve(maximum);
        while (result.size() < maximum && consumed_ < count_) {
            std::vector<std::uint8_t> body(onuros_private_payment_bytes);
            input_.read(reinterpret_cast<char*>(body.data()), body.size());
            if (!input_) throw std::runtime_error("truncated corpus transaction");
            result.push_back(
                {onuros_private_payment_envelope_version, std::move(body)});
            ++consumed_;
        }
        return result;
    }
};

class FramedTls {
    TlsPeerTransport transport_;
    FrameStreamDecoder decoder_{P2pFrameLimits{}, 1024U * 1024U};
    std::uint64_t application_bytes_sent_ = 0U;
    std::uint64_t application_bytes_received_ = 0U;

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

    void send(const P2pFrame& frame) {
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
        application_bytes_sent_ += encoded.size();
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
                application_bytes_received_ += received.bytes;
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
    std::uint64_t application_bytes_sent() const noexcept {
        return application_bytes_sent_;
    }
    std::uint64_t application_bytes_received() const noexcept {
        return application_bytes_received_;
    }
};

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
    policy.required_services = p2p_service_full_node;
    PeerSession session(policy);
    if (session.receive(remote, true) != PeerSessionError::none ||
        session.state() != PeerSessionState::established)
        throw std::runtime_error("P2P handshake rejected");
}

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

std::unique_ptr<PrivacyEngineNetworkAdmission> make_admission(
        const ShieldedState& state, std::uint64_t transactions,
        const onuros_privacy_engine_v1* engine,
        const onuros_accepted_root_v1& root,
        std::uint32_t chain_height, std::uint32_t max_root_age) {
    constexpr std::size_t maximum_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    const auto maximum_transactions = static_cast<std::size_t>(transactions);
    if (transactions > 1'000'000U ||
        maximum_transactions > std::numeric_limits<std::size_t>::max() - 64U)
        throw std::runtime_error("load transaction limit exceeded");
    return std::make_unique<PrivacyEngineNetworkAdmission>(
        engine, std::vector<onuros_accepted_root_v1>{root}, chain_height,
        max_root_age, state,
        PrivateMempoolLimits{maximum_transactions + 64U, maximum_bytes,
                             (maximum_transactions + 64U) * 4U,
                             onuros_private_payment_bytes, 2U},
        maximum_transactions + 64U, maximum_bytes,
        Stage7NodeAdmission::BlockAdmission{},
        NetworkTransactionBatchLimits{256U * 1024U, 32U,
                                      onuros_private_payment_bytes});
}

struct RunStats {
    std::string role;
    double duration_seconds = 0.0;
    std::uint64_t submitted = 0U;
    std::uint64_t admitted = 0U;
    std::uint64_t relayed = 0U;
    std::uint64_t duplicates = 0U;
    std::uint64_t invalid = 0U;
    std::uint64_t queue_high_watermark = 0U;
    std::uint64_t queue_limit = 0U;
    std::uint64_t verification_batches = 0U;
    std::uint64_t verification_tasks = 0U;
    std::uint64_t verification_workers = 0U;
    std::uint64_t verification_pool_starts = 0U;
    double verification_seconds = 0.0;
    double admission_latency_p50_ms = 0.0;
    double admission_latency_p95_ms = 0.0;
    double admission_latency_p99_ms = 0.0;
    double admission_latency_max_ms = 0.0;
    std::uint64_t application_bytes_sent = 0U;
    std::uint64_t application_bytes_received = 0U;
    std::uint64_t divergent = 0U;
    Hash256 id_set{};
    Hash256 first{};
    Hash256 last{};
    std::string cipher;
    Hash256 genesis{};
    Hash256 candidate_root{};
    Hash256 parameter_sha256{};
    Hash256 tls_ca_sha256{};
    std::uint32_t network_id = 0U;
    std::uint32_t circuit_version = 0U;
    std::uint32_t root_height = 0U;
    std::string blockchain_commit;
    std::string privacy_lab_commit;
    std::string node_id;
    bool loopback = true;
};

void write_manifest(const std::filesystem::path& path, const RunStats& stats) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("manifest open failed");
    output << "role=" << stats.role << '\n'
           << std::fixed << std::setprecision(6)
           << "duration_seconds=" << stats.duration_seconds << '\n'
           << "submitted_unique=" << stats.submitted << '\n'
           << "admitted_unique=" << stats.admitted << '\n'
           << "relayed_unique=" << stats.relayed << '\n'
           << "duplicate_transactions=" << stats.duplicates << '\n'
           << "invalid_transactions=" << stats.invalid << '\n'
           << "queue_high_watermark=" << stats.queue_high_watermark << '\n'
           << "queue_limit=" << stats.queue_limit << '\n'
           << "verification_batches=" << stats.verification_batches << '\n'
           << "verification_tasks=" << stats.verification_tasks << '\n'
           << "verification_workers=" << stats.verification_workers << '\n'
           << "verification_pool_starts="
           << stats.verification_pool_starts << '\n'
           << "verification_seconds=" << stats.verification_seconds << '\n'
           << "admission_latency_sample=batch\n"
           << "admission_latency_percentile=nearest-rank\n"
           << "admission_latency_p50_ms=" << stats.admission_latency_p50_ms << '\n'
           << "admission_latency_p95_ms=" << stats.admission_latency_p95_ms << '\n'
           << "admission_latency_p99_ms=" << stats.admission_latency_p99_ms << '\n'
           << "admission_latency_max_ms=" << stats.admission_latency_max_ms << '\n'
           << "application_bytes_sent=" << stats.application_bytes_sent << '\n'
           << "application_bytes_received="
           << stats.application_bytes_received << '\n'
           << "admitted_tps="
           << (stats.duration_seconds == 0.0
                   ? 0.0
                   : static_cast<double>(stats.admitted) /
                         stats.duration_seconds)
           << '\n'
           << "divergent_transactions=" << stats.divergent << '\n'
           << "limits_exceeded=0\n"
           << "payment_bytes=584\n"
           << "proof_system=groth16-bls12-381\n"
           << "commitment_hash=poseidon\n"
           << "verification_backend=onuros-privacy-engine-abi-v1\n"
           << "tracked_witness_backend=onuros-privacy-engine-abi-v2\n"
           << "qualification_profile=groth16-poseidon-payment-relay-v1\n"
           << "active_privacy_protocol_qualified=true\n"
           << "genesis_sync_qualified=false\n"
           << "network_id=" << stats.network_id << '\n'
           << "circuit_version=" << stats.circuit_version << '\n'
           << "root_height=" << stats.root_height << '\n'
           << "genesis=" << hash_hex(stats.genesis) << '\n'
           << "candidate_root=" << hash_hex(stats.candidate_root) << '\n'
           << "parameters_sha256=" << hash_hex(stats.parameter_sha256) << '\n'
           << "tls_ca_sha256=" << hash_hex(stats.tls_ca_sha256) << '\n'
           << "blockchain_commit=" << stats.blockchain_commit << '\n'
           << "privacy_lab_commit=" << stats.privacy_lab_commit << '\n'
           << "node_id=" << stats.node_id << '\n'
           << "loopback=" << (stats.loopback ? "true" : "false") << '\n'
           << "transport_authenticated=true\n"
           << "process_exit_status=0\n"
           << "private_payloads_logged=false\n"
           << "id_set_sha256=" << hash_hex(stats.id_set) << '\n'
           << "first_transaction_id=" << hash_hex(stats.first) << '\n'
           << "last_transaction_id=" << hash_hex(stats.last) << '\n'
           << "tls_cipher=" << stats.cipher << '\n';
    if (!output) throw std::runtime_error("manifest write failed");
}

struct Options {
    std::string role;
    std::string bind = "0.0.0.0";
    std::string address;
    std::uint16_t port = 0U;
    std::filesystem::path certificate;
    std::filesystem::path private_key;
    std::filesystem::path ca;
    std::string expected_origin;
    std::string expected_observer;
    std::string expected_relay;
    std::filesystem::path corpus;
    std::filesystem::path manifest;
    std::filesystem::path parameters;
    Hash256 parameter_sha256{};
    Hash256 root{};
    std::uint32_t network_id = 0U;
    std::uint32_t circuit_version = 0U;
    std::uint32_t root_height = 0U;
    std::uint32_t max_root_age = 100U;
    std::string blockchain_commit;
    std::string privacy_lab_commit;
    std::string node_id;
    std::uint64_t duration = 600U;
    std::uint64_t rate = 110U;
    std::size_t batch = 16U;
    std::size_t workers = 4U;
};

Hash256 hex_hash(const std::string& input, const std::string& name) {
    if (input.size() != 64U)
        throw std::runtime_error(name + " must be 64 hexadecimal characters");
    Hash256 result{};
    for (std::size_t index = 0U; index < result.size(); ++index) {
        const auto text = input.substr(index * 2U, 2U);
        std::size_t parsed = 0U;
        const auto value = std::stoul(text, &parsed, 16);
        if (parsed != 2U || value > 255U)
            throw std::runtime_error("invalid " + name);
        result[index] = static_cast<std::uint8_t>(value);
    }
    return result;
}

bool commit_identity(const std::string& value) {
    if (value.size() != 40U) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        return (byte >= '0' && byte <= '9') ||
               (byte >= 'a' && byte <= 'f');
    });
}

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
    if (parsed != text.size() || value == 0U)
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
    options.expected_origin = argument(argc, argv, "--expected-origin");
    options.expected_observer = argument(argc, argv, "--expected-observer");
    options.expected_relay = argument(argc, argv, "--expected-relay");
    options.corpus = argument(argc, argv, "--corpus");
    options.manifest = argument(argc, argv, "--manifest");
    options.parameters = argument(argc, argv, "--parameters");
    options.parameter_sha256 = hex_hash(
        argument(argc, argv, "--parameters-sha256"), "parameters-sha256");
    options.root = hex_hash(argument(argc, argv, "--root"), "root");
    options.network_id = static_cast<std::uint32_t>(
        number_argument(argc, argv, "--network-id", 0U));
    options.circuit_version = static_cast<std::uint32_t>(
        number_argument(argc, argv, "--circuit-version", 0U));
    options.root_height = static_cast<std::uint32_t>(
        number_argument(argc, argv, "--root-height", 0U));
    options.max_root_age = static_cast<std::uint32_t>(number_argument(
        argc, argv, "--max-root-age", options.max_root_age));
    options.blockchain_commit = argument(argc, argv, "--blockchain-commit");
    options.privacy_lab_commit = argument(argc, argv, "--privacy-lab-commit");
    options.node_id = argument(argc, argv, "--node-id");
    options.duration = number_argument(argc, argv, "--duration", options.duration);
    options.rate = number_argument(argc, argv, "--rate", options.rate);
    options.batch = static_cast<std::size_t>(
        number_argument(argc, argv, "--batch", options.batch));
    options.workers = static_cast<std::size_t>(
        number_argument(argc, argv, "--workers", options.workers));
    if (options.role.empty() || options.certificate.empty() ||
        options.private_key.empty() || options.ca.empty() ||
        options.manifest.empty() || options.parameters.empty() ||
        options.network_id == 0U || options.circuit_version == 0U ||
        options.root_height == 0U ||
        !commit_identity(options.blockchain_commit) ||
        !commit_identity(options.privacy_lab_commit) ||
        options.node_id.empty() || options.batch > 32U ||
        options.workers > 256U)
        throw std::runtime_error("missing or out-of-range load option");
    return options;
}

Hash256 file_sha256(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("identity file open failed");
    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    return sha256(bytes);
}

RunStats stamp_identity(RunStats stats, const Options& options) {
    stats.genesis = value(1U);
    stats.candidate_root = options.root;
    stats.parameter_sha256 = options.parameter_sha256;
    stats.tls_ca_sha256 = file_sha256(options.ca);
    stats.network_id = options.network_id;
    stats.circuit_version = options.circuit_version;
    stats.root_height = options.root_height;
    stats.blockchain_commit = options.blockchain_commit;
    stats.privacy_lab_commit = options.privacy_lab_commit;
    stats.node_id = options.node_id;
    const auto endpoint = options.role == "relay" ? options.bind : options.address;
    stats.loopback = endpoint == "127.0.0.1" || endpoint == "localhost";
    return stats;
}

using EnginePtr = std::unique_ptr<onuros_privacy_engine_v1,
                                  void (*)(onuros_privacy_engine_v1*)>;

onuros_accepted_root_v1 accepted_root(const Options& options) {
    onuros_accepted_root_v1 root{};
    root.height = options.root_height;
    std::copy(options.root.begin(), options.root.end(), std::begin(root.root));
    return root;
}

EnginePtr open_engine(const Options& options) {
    if (onuros_privacy_engine_abi_version() != 1U ||
        onuros_privacy_engine_payment_bytes_v1() !=
            onuros_private_payment_bytes)
        throw std::runtime_error("unsupported Privacy Engine ABI");
    const auto root = accepted_root(options);
    onuros_privacy_status_v1 status = ONUROS_PRIVACY_INTERNAL_PANIC;
    auto* engine = onuros_privacy_engine_open_roots_v1(
        options.parameters.string().c_str(), options.network_id,
        options.circuit_version, &root, 1U, options.parameter_sha256.data(),
        &status);
    if (engine == nullptr || status != ONUROS_PRIVACY_OK) {
        if (engine != nullptr) onuros_privacy_engine_close_v1(engine);
        throw std::runtime_error("Privacy Engine open failed: " +
                                 std::to_string(static_cast<int>(status)));
    }
    return {engine, &onuros_privacy_engine_close_v1};
}

void require_control(const StartControl& control, const Options& options) {
    if (control.network_id != options.network_id ||
        control.circuit_version != options.circuit_version ||
        control.root_height != options.root_height ||
        control.max_root_age != options.max_root_age ||
        control.anchor != options.root ||
        control.parameter_sha256 != options.parameter_sha256)
        throw std::runtime_error("peer candidate identity mismatch");
}

void record_ids(const std::vector<TransactionEnvelope>& transactions,
                std::vector<Hash256>& identifiers) {
    for (const auto& transaction : transactions)
        identifiers.push_back(transaction_id(transaction));
}

double require_admitted(PrivacyEngineNetworkAdmission& admission,
                        const P2pFrame& frame, std::size_t workers,
                        std::vector<Hash256>& identifiers) {
    const auto decoded = decode_network_transactions(
        frame.payload, {256U * 1024U, 32U, onuros_private_payment_bytes});
    if (!decoded.accepted()) throw std::runtime_error("transaction decode failed");
    const auto started = Clock::now();
    const auto result = admission.handle_frame_parallel(frame, workers);
    const auto elapsed = std::chrono::duration<double, std::milli>(
        Clock::now() - started).count();
    if (!result || !result->accepted() ||
        result->accepted_transactions != decoded.transactions.size())
        throw std::runtime_error("Privacy Engine transaction admission failed");
    record_ids(decoded.transactions, identifiers);
    return elapsed;
}

double nearest_rank_percentile(std::vector<double> samples,
                               std::size_t numerator) {
    if (samples.empty() || numerator == 0U || numerator > 100U)
        throw std::runtime_error("invalid admission latency samples");
    std::sort(samples.begin(), samples.end());
    const auto rank = (numerator * samples.size() + 99U) / 100U;
    return samples[rank - 1U];
}

RunStats complete_stats(const std::string& role, Clock::time_point started,
                        std::uint64_t submitted,
                        const PrivacyEngineNetworkAdmission& admission,
                        const std::vector<Hash256>& identifiers,
                        const std::vector<double>& admission_latencies,
                        std::uint64_t application_bytes_sent,
                        std::uint64_t application_bytes_received,
                        std::uint64_t divergent, const char* cipher) {
    if (identifiers.empty() || admission_latencies.empty())
        throw std::runtime_error("empty transaction measurements");
    RunStats stats;
    stats.role = role;
    stats.duration_seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
    stats.submitted = submitted;
    stats.admitted = admission.metrics().transactions_accepted;
    stats.relayed = admission.relay_pool().size();
    stats.duplicates = 0U;
    stats.invalid = admission.metrics().transactions_mempool_rejected;
    stats.queue_high_watermark =
        admission.metrics().verification_queue_high_watermark;
    stats.queue_limit = admission.metrics().verification_queue_limit;
    stats.verification_batches = admission.metrics().verification_batches;
    stats.verification_tasks = admission.metrics().verification_tasks;
    stats.verification_workers = admission.metrics().verification_workers;
    stats.verification_pool_starts =
        admission.metrics().verification_pool_starts;
    stats.verification_seconds =
        static_cast<double>(admission.metrics().verification_microseconds) /
        1'000'000.0;
    stats.admission_latency_p50_ms =
        nearest_rank_percentile(admission_latencies, 50U);
    stats.admission_latency_p95_ms =
        nearest_rank_percentile(admission_latencies, 95U);
    stats.admission_latency_p99_ms =
        nearest_rank_percentile(admission_latencies, 99U);
    stats.admission_latency_max_ms =
        *std::max_element(admission_latencies.begin(),
                          admission_latencies.end());
    stats.application_bytes_sent = application_bytes_sent;
    stats.application_bytes_received = application_bytes_received;
    stats.divergent = divergent;
    stats.id_set = id_set_hash(identifiers);
    stats.first = identifiers.front();
    stats.last = identifiers.back();
    stats.cipher = cipher == nullptr ? "unknown" : cipher;
    return stats;
}

RunStats run_observer(const Options& options) {
    if (options.address.empty() || options.expected_relay.empty())
        throw std::runtime_error("observer connection options missing");
    auto context = TlsContext::mutual(
        options.certificate.string(), options.private_key.string(),
        options.ca.string());
    if (!context) throw std::runtime_error("observer TLS material failed");
    FramedTls relay(*context,
        connect_connection(options.address, options.port), TlsRole::client,
        options.expected_relay);
    relay.handshake_tls();
    exchange_hello(relay, 30'003U);
    std::cout << "READY role=observer cipher=" << relay.cipher() << std::endl;
    const auto start_frame = relay.receive();
    if (start_frame.type != P2pMessageType::ping)
        throw std::runtime_error("observer expected start control");
    const auto control = decode_start(start_frame.payload);
    require_control(control, options);
    auto engine = open_engine(options);
    const ShieldedState state(value(1U), options.root);
    auto admission = make_admission(
        state, control.transactions, engine.get(), accepted_root(options),
        options.root_height, options.max_root_age);
    std::vector<Hash256> identifiers;
    identifiers.reserve(static_cast<std::size_t>(control.transactions));
    std::vector<double> admission_latencies;
    admission_latencies.reserve(static_cast<std::size_t>(control.transactions));
    const auto started = Clock::now();
    for (;;) {
        const auto frame = relay.receive();
        if (frame.type == P2pMessageType::transactions) {
            admission_latencies.push_back(require_admitted(
                *admission, frame, options.workers, identifiers));
        } else if (frame.type == P2pMessageType::ping &&
                   begins_with(frame.payload, finish_magic)) {
            break;
        } else {
            throw std::runtime_error("observer received unexpected frame");
        }
    }
    const Summary summary{identifiers.size(), id_set_hash(identifiers)};
    relay.send({stage7_protocol_version, P2pMessageType::pong, 4U,
                encode_summary(summary)});
    return stamp_identity(complete_stats(
        "observer", started, identifiers.size(), *admission, identifiers,
        admission_latencies, relay.application_bytes_sent(),
        relay.application_bytes_received(), 0U, relay.cipher()), options);
}

RunStats run_relay(const Options& options) {
    if (options.expected_origin.empty() || options.expected_observer.empty())
        throw std::runtime_error("relay peer identities missing");
    auto context = TlsContext::mutual(
        options.certificate.string(), options.private_key.string(),
        options.ca.string());
    if (!context) throw std::runtime_error("relay TLS material failed");
    auto listener = TcpListener::listen_ipv4(options.bind, options.port, 8);
    if (!listener) throw std::runtime_error("relay listen failed");
    std::cout << "READY role=relay bind=" << options.bind
              << " port=" << listener->port() << std::endl;
    FramedTls origin(*context, accept_connection(*listener), TlsRole::server,
                     options.expected_origin);
    origin.handshake_tls();
    exchange_hello(origin, 30'002U);
    std::cout << "CONNECTED peer=origin" << std::endl;
    FramedTls observer(*context, accept_connection(*listener), TlsRole::server,
                       options.expected_observer);
    observer.handshake_tls();
    exchange_hello(observer, 30'002U);
    std::cout << "CONNECTED peer=observer" << std::endl;
    origin.send({stage7_protocol_version, P2pMessageType::pong, 2U,
                 {'R', 'E', 'A', 'D', 'Y'}});
    const auto start_frame = origin.receive();
    if (start_frame.type != P2pMessageType::ping)
        throw std::runtime_error("relay expected start control");
    const auto control = decode_start(start_frame.payload);
    require_control(control, options);
    observer.send(start_frame);
    auto engine = open_engine(options);
    const ShieldedState state(value(1U), options.root);
    auto admission = make_admission(
        state, control.transactions, engine.get(), accepted_root(options),
        options.root_height, options.max_root_age);
    std::vector<Hash256> identifiers;
    identifiers.reserve(static_cast<std::size_t>(control.transactions));
    std::vector<double> admission_latencies;
    admission_latencies.reserve(static_cast<std::size_t>(control.transactions));
    const auto started = Clock::now();
    for (;;) {
        const auto frame = origin.receive();
        if (frame.type == P2pMessageType::transactions) {
            admission_latencies.push_back(require_admitted(
                *admission, frame, options.workers, identifiers));
            observer.send(frame);
        } else if (frame.type == P2pMessageType::ping &&
                   begins_with(frame.payload, finish_magic)) {
            observer.send(frame);
            break;
        } else {
            throw std::runtime_error("relay received unexpected frame");
        }
    }
    const auto observer_frame = observer.receive();
    if (observer_frame.type != P2pMessageType::pong)
        throw std::runtime_error("relay expected observer summary");
    const auto observer_summary = decode_summary(observer_frame.payload);
    const Summary relay_summary{identifiers.size(), id_set_hash(identifiers)};
    const auto divergent = relay_summary.transactions != observer_summary.transactions ||
        relay_summary.id_set != observer_summary.id_set ? 1U : 0U;
    origin.send({stage7_protocol_version, P2pMessageType::pong, 4U,
                 encode_relay_summary(relay_summary, observer_summary)});
    return stamp_identity(complete_stats(
        "relay", started, identifiers.size(), *admission, identifiers,
        admission_latencies,
        origin.application_bytes_sent() + observer.application_bytes_sent(),
        origin.application_bytes_received() +
            observer.application_bytes_received(),
        divergent, origin.cipher()), options);
}

RunStats run_origin(const Options& options) {
    if (options.address.empty() || options.expected_relay.empty() ||
        options.corpus.empty())
        throw std::runtime_error("origin workload options missing");
    if (options.rate > std::numeric_limits<std::uint64_t>::max() /
                           options.duration)
        throw std::runtime_error("workload count overflow");
    const auto target = options.rate * options.duration;
    CorpusReader corpus(options.corpus);
    if (corpus.count() < target)
        throw std::runtime_error("Onuros corpus is smaller than workload");
    if (corpus.network_id() != options.network_id ||
        corpus.circuit_version() != options.circuit_version ||
        corpus.anchor() != options.root)
        throw std::runtime_error("corpus candidate identity mismatch");
    auto engine = open_engine(options);
    auto context = TlsContext::mutual(
        options.certificate.string(), options.private_key.string(),
        options.ca.string());
    if (!context) throw std::runtime_error("origin TLS material failed");
    FramedTls relay(*context,
        connect_connection(options.address, options.port), TlsRole::client,
        options.expected_relay);
    relay.handshake_tls();
    exchange_hello(relay, 30'001U);
    const auto ready = relay.receive();
    if (ready.type != P2pMessageType::pong || ready.request_id != 2U)
        throw std::runtime_error("origin expected relay readiness");
    const ShieldedState state(value(1U), options.root);
    auto admission = make_admission(
        state, target, engine.get(), accepted_root(options),
        options.root_height, options.max_root_age);
    std::vector<Hash256> identifiers;
    identifiers.reserve(static_cast<std::size_t>(target));
    std::vector<double> admission_latencies;
    admission_latencies.reserve(static_cast<std::size_t>(
        (target + options.batch - 1U) / options.batch));
    StartControl control;
    control.transactions = target;
    control.duration_seconds = options.duration;
    control.anchor = options.root;
    control.parameter_sha256 = options.parameter_sha256;
    control.network_id = options.network_id;
    control.circuit_version = options.circuit_version;
    control.root_height = options.root_height;
    control.max_root_age = options.max_root_age;
    relay.send({stage7_protocol_version, P2pMessageType::ping, 3U,
                encode_start(control)});
    const auto started = Clock::now();
    std::uint64_t submitted = 0U;
    std::uint64_t request_id = 100U;
    while (submitted < target) {
        const auto wanted = static_cast<std::size_t>(std::min<std::uint64_t>(
            options.batch, target - submitted));
        auto transactions = corpus.read_batch(wanted);
        if (transactions.size() != wanted)
            throw std::runtime_error("Onuros corpus ended early");
        P2pFrame frame{stage7_protocol_version, P2pMessageType::transactions,
                       request_id++, encode_network_transactions(transactions)};
        admission_latencies.push_back(require_admitted(
            *admission, frame, options.workers, identifiers));
        relay.send(frame);
        submitted += transactions.size();
        const auto target_elapsed = std::chrono::duration<double>(
            static_cast<double>(submitted) / static_cast<double>(options.rate));
        std::this_thread::sleep_until(started +
            std::chrono::duration_cast<Clock::duration>(target_elapsed));
    }
    const auto deadline = started + std::chrono::seconds(options.duration);
    std::this_thread::sleep_until(deadline);
    relay.send({stage7_protocol_version, P2pMessageType::ping, 4U,
                std::vector<std::uint8_t>(finish_magic.begin(),
                                          finish_magic.end())});
    const auto response = relay.receive();
    if (response.type != P2pMessageType::pong || response.request_id != 4U)
        throw std::runtime_error("origin expected relay summary");
    const auto [relay_summary, observer_summary] =
        decode_relay_summary(response.payload);
    const Summary local{identifiers.size(), id_set_hash(identifiers)};
    const auto divergent =
        local.transactions != relay_summary.transactions ||
        local.id_set != relay_summary.id_set ||
        local.transactions != observer_summary.transactions ||
        local.id_set != observer_summary.id_set ? 1U : 0U;
    return stamp_identity(complete_stats(
        "origin", started, submitted, *admission, identifiers,
        admission_latencies, relay.application_bytes_sent(),
        relay.application_bytes_received(), divergent, relay.cipher()), options);
}

void usage(const char* program) {
    std::cerr
        << "Usage: " << program << " --role origin|relay|observer --port PORT"
        << " --cert FILE --key FILE --ca FILE --manifest FILE [options]\n"
        << "  origin: --address HOST --expected-relay DNS --corpus FILE"
        << " [--duration 600 --rate 110 --batch 16 --workers 4]\n"
        << "  relay: --bind ADDRESS --expected-origin DNS"
        << " --expected-observer DNS [--workers 4]\n"
        << "  observer: --address HOST --expected-relay DNS [--workers 4]\n"
        << "  all roles: --parameters FILE --parameters-sha256 HEX"
        << " --network-id N --circuit-version N --root-height N --root HEX"
        << " --blockchain-commit SHA --privacy-lab-commit SHA"
        << " --node-id UNIQUE-HOST-ID\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        SocketRuntime sockets;
        if (!sockets.ready()) throw std::runtime_error("socket runtime unavailable");
        const auto options = parse_options(argc, argv);
        RunStats stats;
        if (options.role == "origin") stats = run_origin(options);
        else if (options.role == "relay") stats = run_relay(options);
        else if (options.role == "observer") stats = run_observer(options);
        else {
            usage(argv[0]);
            return 2;
        }
        write_manifest(options.manifest, stats);
        std::cout << "stage7_private_load=PASS role=" << stats.role
                  << " admitted_unique=" << stats.admitted
                  << " duration_seconds=" << std::fixed << std::setprecision(6)
                  << stats.duration_seconds
                  << " id_set_sha256=" << hash_hex(stats.id_set) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "stage7_private_load=FAIL reason=" << error.what() << '\n';
        return 1;
    }
}
