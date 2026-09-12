#include "onuros/mining_endpoint.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace onuros;

namespace {
unsigned checks = 0U;

void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}

std::string hex(const std::vector<std::uint8_t>& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(bytes.size() * 2U, '0');
    for (std::size_t i = 0U; i < bytes.size(); ++i) {
        result[i * 2U] = digits[bytes[i] >> 4U];
        result[i * 2U + 1U] = digits[bytes[i] & 0x0fU];
    }
    return result;
}

LocalNodeParameters parameters() {
    const auto limit = decode_compact_target(0x207fffffU);
    if (!limit) throw std::runtime_error("test target");
    LocalNodeParameters value;
    value.validation_limits = {1U, 1U, 4096U, 16U, 1024U};
    value.decode_limits = {4096U, 16U, 1024U};
    value.difficulty = {60U, 60U, 4U, *limit};
    value.max_database_bytes = 1U << 20U;
    return value;
}
} // namespace

int main() {
    const auto database = std::filesystem::temp_directory_path() /
                          "onuros-mining-endpoint-test.db";
    auto temporary = database;
    temporary += ".tmp";
    std::error_code ignored;
    std::filesystem::remove(database, ignored);
    std::filesystem::remove(temporary, ignored);
    try {
        MiningJob fixture{9U, 30'000U, {}, 0x207fffffU};
        fixture.header_hash[0] = 17U;
        const auto encoded_job = encode_mining_job(fixture);
        check(hex(encoded_job) ==
              "0100010009000000000000003075000000000000110000000000000000000000"
              "0000000000000000000000000000000000000000ffff7f20",
              "miner-compatible job golden vector");
        const auto decoded_job = decode_mining_job(encoded_job);
        check(encoded_job.size() == mining_job_encoded_size && decoded_job &&
              decoded_job->job_id == fixture.job_id &&
              decoded_job->height == fixture.height &&
              decoded_job->header_hash == fixture.header_hash &&
              decoded_job->compact_target == fixture.compact_target,
              "mining job round trip");
        auto malformed = encoded_job;
        malformed.push_back(0U);
        check(!decode_mining_job(malformed), "job trailing byte rejected");
        malformed = encoded_job;
        malformed[0] = 2U;
        check(!decode_mining_job(malformed), "job version rejected");

        MiningSolution solution_fixture{9U, 42U, {}};
        solution_fixture.mix_hash[3] = 11U;
        const auto encoded_solution = encode_mining_solution(solution_fixture);
        check(hex(encoded_solution) ==
              "0100010009000000000000002a000000000000000000000b0000000000000000"
              "0000000000000000000000000000000000000000",
              "miner-compatible solution golden vector");
        const auto decoded_solution = decode_mining_solution(encoded_solution);
        check(encoded_solution.size() == mining_solution_encoded_size &&
              decoded_solution && decoded_solution->job_id == 9U &&
              decoded_solution->nonce == 42U &&
              decoded_solution->mix_hash == solution_fixture.mix_hash,
              "mining solution round trip");
        malformed = encoded_solution;
        malformed[2] = 2U;
        check(!decode_mining_solution(malformed), "solution algorithm rejected");

        MiningResultMessage result_fixture{9U, MiningResultCode::accepted,
                                            fixture.header_hash};
        const auto encoded_result = encode_mining_result(result_fixture);
        const auto decoded_result = decode_mining_result(encoded_result);
        check(encoded_result.size() == mining_result_encoded_size &&
              decoded_result && decoded_result->job_id == 9U &&
              decoded_result->code == MiningResultCode::accepted &&
              decoded_result->block_identifier == fixture.header_hash,
              "mining result round trip");
        malformed = encoded_result;
        malformed[2] = 9U;
        check(!decode_mining_result(malformed), "unknown result code rejected");
        malformed = encoded_result;
        malformed[2] = static_cast<std::uint8_t>(MiningResultCode::rejected);
        check(!decode_mining_result(malformed),
              "rejection carrying block identifier rejected");

        KawpowMiningEndpoint endpoint(parameters());
        check(endpoint.open(database).error == LocalNodeError::none,
              "KawPoW endpoint opens");
        const auto job = endpoint.issue_job({{1U, {1U}}}, 100U);
        check(job && job->job_id != 0U && job->height == 0U,
              "genesis mining job issued");

        MiningSolution solution{job->job_id, 0U, {}};
        for (;;) {
            const auto proof = calculate_kawpow(job->height, job->header_hash,
                                                solution.nonce);
            check(proof.has_value(), "CPU candidate proof available");
            solution.mix_hash = proof->mix_hash;
            const auto limit = decode_compact_target(job->compact_target);
            check(limit.has_value(), "job target decodes");
            if (hash_meets_compact_target(proof->final_hash,
                    job->compact_target, *limit))
                break;
            ++solution.nonce;
        }

        auto wrong_job = solution;
        ++wrong_job.job_id;
        check(endpoint.submit(wrong_job, 100U).error ==
                  MiningSubmitError::stale_job,
              "unknown job rejected before consensus");
        auto altered = solution;
        altered.mix_hash[0] ^= 1U;
        const auto invalid = endpoint.submit(altered, 100U);
        check(invalid.error == MiningSubmitError::invalid_proof &&
              endpoint.node().store().blocks().empty(),
              "altered GPU mix rejected without persistence");
        const auto accepted = endpoint.submit(solution, 100U);
        check(accepted.error == MiningSubmitError::none &&
              endpoint.node().store().blocks().size() == 1U,
              "CPU-verified GPU-style solution persisted");
        check(endpoint.submit(solution, 100U).error ==
                  MiningSubmitError::no_active_job,
              "accepted job cannot be replayed");

        const auto message = make_mining_result_message(job->job_id, accepted);
        check(message.code == MiningResultCode::accepted &&
              message.block_identifier == accepted.block_identifier,
              "accepted endpoint result encoded for miner");

        std::filesystem::remove(database, ignored);
        std::filesystem::remove(temporary, ignored);
        std::cout << checks << " mining-endpoint checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(database, ignored);
        std::filesystem::remove(temporary, ignored);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
