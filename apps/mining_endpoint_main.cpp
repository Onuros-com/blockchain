#include "onuros/mining_endpoint.hpp"
#include "onuros/p2p_transport.hpp"

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

LocalNodeParameters parameters() {
    const auto limit = decode_compact_target(0x207fffffU);
    if (!limit) throw std::runtime_error("invalid KawPoW test target");
    LocalNodeParameters value;
    value.validation_limits = {1U, 1U, max_serialized_block_bytes,
                               2'000U, 16U * 1024U};
    value.decode_limits = {max_serialized_block_bytes, 2'000U, 16U * 1024U};
    value.difficulty = {60U, 60U, 4U, *limit};
    value.max_database_bytes = 64U << 20U;
    return value;
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

void send_frame(TcpConnection& connection, P2pMessageType type,
                std::uint64_t request_id,
                const std::vector<std::uint8_t>& payload) {
    if (!send_all(connection, encode_p2p_frame(
            {stage7_protocol_version, type, request_id, payload})))
        throw std::runtime_error("mining endpoint send failed");
}

P2pFrame receive_frame(TcpConnection& connection, FrameStreamDecoder& stream) {
    std::array<std::uint8_t, 4096U> bytes{};
    for (unsigned attempt = 0U; attempt < 30'000U; ++attempt) {
        const auto parsed = stream.next();
        if (parsed.status == FrameStreamStatus::frame_ready) return parsed.frame;
        if (parsed.status == FrameStreamStatus::failed)
            throw std::runtime_error("invalid mining frame");
        const auto received = connection.receive_some(bytes.data(), bytes.size());
        if (received.status == SocketIoStatus::ok) {
            if (!stream.feed(bytes.data(), received.bytes))
                throw std::runtime_error("mining receive queue exceeded limit");
        } else if (received.status != SocketIoStatus::would_block) {
            throw std::runtime_error("mining peer disconnected");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("mining message timeout");
}

std::optional<TcpConnection> connect_with_retry(std::uint16_t port) {
    for (unsigned attempt = 0U; attempt < 10'000U; ++attempt) {
        auto connection = TcpConnection::connect_ipv4("127.0.0.1", port);
        if (connection) return connection;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

void run_server(const std::filesystem::path& data, std::uint16_t port,
                unsigned maximum_submissions) {
    KawpowMiningEndpoint endpoint(parameters());
    if (endpoint.open(data).error != LocalNodeError::none)
        throw std::runtime_error("mining database open failed");
    const auto* tip = endpoint.node().store().index().active_tip();
    const auto timestamp = tip == nullptr ? 100U :
        endpoint.node().store().find(tip->id)->block.header.timestamp + 60U;
    const auto marker = static_cast<std::uint8_t>(tip == nullptr ? 0U :
                                                  tip->height + 1U);
    const auto job = endpoint.issue_job({{1U, {marker}}}, timestamp);
    if (!job) throw std::runtime_error("could not issue mining job");
    auto listener = TcpListener::listen_loopback(port);
    if (!listener) throw std::runtime_error("mining listener failed");
    std::cout << "READY port=" << listener->port()
              << " job=" << job->job_id << std::endl;

    std::optional<TcpConnection> connection;
    for (unsigned attempt = 0U; attempt < 10'000U && !connection; ++attempt) {
        connection = listener->accept_one();
        if (!connection)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!connection) throw std::runtime_error("mining accept timeout");
    send_frame(*connection, P2pMessageType::mining_job, job->job_id,
               encode_mining_job(*job));

    FrameStreamDecoder stream({}, 4096U);
    for (unsigned attempt = 0U; attempt < maximum_submissions; ++attempt) {
        const auto frame = receive_frame(*connection, stream);
        if (frame.type != P2pMessageType::mining_solution ||
            frame.request_id != job->job_id)
            throw std::runtime_error("unexpected mining solution frame");
        const auto solution = decode_mining_solution(frame.payload);
        MiningSubmitResult submitted{MiningSubmitError::node_rejected};
        if (solution) submitted = endpoint.submit(*solution, timestamp);
        const auto result = make_mining_result_message(job->job_id, submitted);
        send_frame(*connection, P2pMessageType::mining_result, job->job_id,
                   encode_mining_result(result));
        std::cout << (result.code == MiningResultCode::accepted ?
                      "ACCEPTED" : "REJECTED")
                  << " job=" << job->job_id
                  << " height=" << job->height
                  << " submission=" << attempt + 1U
                  << " result_code=" << static_cast<unsigned>(result.code)
                  << " block=" << hash_hex(result.block_identifier) << std::endl;
        if (result.code == MiningResultCode::accepted) break;
    }
}

void run_client(std::uint16_t port, bool alter_mix, bool negative_probe) {
    auto connection = connect_with_retry(port);
    if (!connection) throw std::runtime_error("mining connect timeout");
    FrameStreamDecoder stream({}, 4096U);
    const auto frame = receive_frame(*connection, stream);
    if (frame.type != P2pMessageType::mining_job)
        throw std::runtime_error("expected mining job frame");
    const auto job = decode_mining_job(frame.payload);
    if (!job || frame.request_id != job->job_id)
        throw std::runtime_error("invalid mining job");
    const auto limit = decode_compact_target(job->compact_target);
    if (!limit) throw std::runtime_error("invalid mining target");

    MiningSolution solution{job->job_id, 0U, {}};
    bool solved = false;
    for (std::uint64_t attempt = 0U; attempt < 10'000U; ++attempt) {
        const auto proof = calculate_kawpow(job->height, job->header_hash,
                                            solution.nonce);
        if (!proof) throw std::runtime_error("KawPoW calculation failed");
        solution.mix_hash = proof->mix_hash;
        if (hash_meets_compact_target(proof->final_hash,
                job->compact_target, *limit)) {
            solved = true;
            break;
        }
        ++solution.nonce;
    }
    if (!solved) throw std::runtime_error("KawPoW nonce limit exhausted");
    if (negative_probe) {
        auto invalid = solution;
        invalid.mix_hash[0] ^= 1U;
        send_frame(*connection, P2pMessageType::mining_solution, job->job_id,
                   encode_mining_solution(invalid));
        const auto invalid_frame = receive_frame(*connection, stream);
        const auto invalid_result = decode_mining_result(invalid_frame.payload);
        if (invalid_frame.type != P2pMessageType::mining_result ||
            invalid_frame.request_id != job->job_id || !invalid_result ||
            invalid_result->code != MiningResultCode::invalid_proof)
            throw std::runtime_error("negative probe was not rejected");
        std::cout << "EXPECTED_REJECTION job=" << job->job_id << std::endl;
    }
    if (alter_mix) solution.mix_hash[0] ^= 1U;
    send_frame(*connection, P2pMessageType::mining_solution, job->job_id,
               encode_mining_solution(solution));
    const auto result_frame = receive_frame(*connection, stream);
    if (result_frame.type != P2pMessageType::mining_result ||
        result_frame.request_id != job->job_id)
        throw std::runtime_error("unexpected mining result frame");
    const auto result = decode_mining_result(result_frame.payload);
    if (!result) throw std::runtime_error("invalid mining result");
    const auto expected = alter_mix ? MiningResultCode::invalid_proof :
                                      MiningResultCode::accepted;
    if (result->code != expected)
        throw std::runtime_error("unexpected mining decision");
    std::cout << (result->code == MiningResultCode::accepted ?
                  "ACCEPTED" : "REJECTED")
              << " job=" << result->job_id << std::endl;
}

void usage(const char* program) {
    std::cerr << "Usage: " << program
              << " --role server|client --port PORT [--data PATH]"
                 " [--alter-mix|--negative-probe] [--submissions COUNT]\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string role;
        std::filesystem::path data;
        std::uint16_t port = 0U;
        unsigned maximum_submissions = 1U;
        bool alter_mix = false;
        bool negative_probe = false;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--role" && i + 1 < argc) role = argv[++i];
            else if (argument == "--data" && i + 1 < argc) data = argv[++i];
            else if (argument == "--port" && i + 1 < argc) {
                const auto value = std::stoul(argv[++i]);
                if (value == 0U || value > 65'535U)
                    throw std::invalid_argument("invalid port");
                port = static_cast<std::uint16_t>(value);
            } else if (argument == "--alter-mix") alter_mix = true;
            else if (argument == "--negative-probe") negative_probe = true;
            else if (argument == "--submissions" && i + 1 < argc) {
                const auto value = std::stoul(argv[++i]);
                if (value == 0U || value > 100U)
                    throw std::invalid_argument("invalid submission count");
                maximum_submissions = static_cast<unsigned>(value);
            }
            else throw std::invalid_argument("unknown or incomplete argument");
        }
        if (port == 0U || (role != "server" && role != "client") ||
            (role == "server" && data.empty()) ||
            (role == "server" && (alter_mix || negative_probe)) ||
            (alter_mix && negative_probe))
            throw std::invalid_argument("required argument missing");
        SocketRuntime runtime;
        if (!runtime.ready()) throw std::runtime_error("socket runtime failed");
        if (role == "server") run_server(data, port, maximum_submissions);
        else run_client(port, alter_mix, negative_probe);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        usage(argv[0]);
        return 1;
    }
}
