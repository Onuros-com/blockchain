#pragma once

#include "onuros/block_format.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace onuros {

inline constexpr std::uint16_t mining_protocol_version = 1U;
inline constexpr std::uint16_t mining_algorithm_kawpow = 1U;
inline constexpr std::size_t mining_job_encoded_size = 56U;
inline constexpr std::size_t mining_solution_encoded_size = 52U;
inline constexpr std::size_t mining_result_encoded_size = 44U;

struct MiningJob {
    std::uint64_t job_id = 0U;
    Height height = 0U;
    Hash256 header_hash{};
    std::uint32_t compact_target = 0U;
};

struct MiningSolution {
    std::uint64_t job_id = 0U;
    std::uint64_t nonce = 0U;
    Hash256 mix_hash{};
};

enum class MiningResultCode : std::uint16_t {
    accepted = 0U,
    stale_job = 1U,
    invalid_proof = 2U,
    rejected = 3U,
    rate_limited = 4U
};

struct MiningResultMessage {
    std::uint64_t job_id = 0U;
    MiningResultCode code = MiningResultCode::rejected;
    Hash256 block_identifier{};
};

inline std::vector<std::uint8_t> encode_mining_job(const MiningJob& job) {
    std::vector<std::uint8_t> output;
    output.reserve(mining_job_encoded_size);
    detail::append_little_endian(output, mining_protocol_version);
    detail::append_little_endian(output, mining_algorithm_kawpow);
    detail::append_little_endian(output, job.job_id);
    detail::append_little_endian(output, job.height);
    detail::append_hash(output, job.header_hash);
    detail::append_little_endian(output, job.compact_target);
    return output;
}

inline std::optional<MiningJob> decode_mining_job(
        const std::vector<std::uint8_t>& input) {
    if (input.size() != mining_job_encoded_size) return std::nullopt;
    detail::ByteReader reader(input);
    std::uint16_t version = 0U;
    std::uint16_t algorithm = 0U;
    MiningJob job;
    if (!reader.read_little_endian(version) ||
        !reader.read_little_endian(algorithm) ||
        version != mining_protocol_version ||
        algorithm != mining_algorithm_kawpow ||
        !reader.read_little_endian(job.job_id) || job.job_id == 0U ||
        !reader.read_little_endian(job.height) ||
        !reader.read_hash(job.header_hash) ||
        !reader.read_little_endian(job.compact_target) ||
        job.compact_target == 0U || !reader.exhausted())
        return std::nullopt;
    return job;
}

inline std::vector<std::uint8_t> encode_mining_solution(
        const MiningSolution& solution) {
    std::vector<std::uint8_t> output;
    output.reserve(mining_solution_encoded_size);
    detail::append_little_endian(output, mining_protocol_version);
    detail::append_little_endian(output, mining_algorithm_kawpow);
    detail::append_little_endian(output, solution.job_id);
    detail::append_little_endian(output, solution.nonce);
    detail::append_hash(output, solution.mix_hash);
    return output;
}

inline std::optional<MiningSolution> decode_mining_solution(
        const std::vector<std::uint8_t>& input) {
    if (input.size() != mining_solution_encoded_size) return std::nullopt;
    detail::ByteReader reader(input);
    std::uint16_t version = 0U;
    std::uint16_t algorithm = 0U;
    MiningSolution solution;
    if (!reader.read_little_endian(version) ||
        !reader.read_little_endian(algorithm) ||
        version != mining_protocol_version ||
        algorithm != mining_algorithm_kawpow ||
        !reader.read_little_endian(solution.job_id) || solution.job_id == 0U ||
        !reader.read_little_endian(solution.nonce) ||
        !reader.read_hash(solution.mix_hash) || !reader.exhausted())
        return std::nullopt;
    return solution;
}

inline bool known_mining_result_code(std::uint16_t value) noexcept {
    return value <= static_cast<std::uint16_t>(MiningResultCode::rate_limited);
}

inline std::vector<std::uint8_t> encode_mining_result(
        const MiningResultMessage& result) {
    std::vector<std::uint8_t> output;
    output.reserve(mining_result_encoded_size);
    detail::append_little_endian(output, mining_protocol_version);
    detail::append_little_endian(output,
        static_cast<std::uint16_t>(result.code));
    detail::append_little_endian(output, result.job_id);
    detail::append_hash(output, result.block_identifier);
    return output;
}

inline std::optional<MiningResultMessage> decode_mining_result(
        const std::vector<std::uint8_t>& input) {
    if (input.size() != mining_result_encoded_size) return std::nullopt;
    detail::ByteReader reader(input);
    std::uint16_t version = 0U;
    std::uint16_t code = 0U;
    MiningResultMessage result;
    if (!reader.read_little_endian(version) || version != mining_protocol_version ||
        !reader.read_little_endian(code) || !known_mining_result_code(code) ||
        !reader.read_little_endian(result.job_id) || result.job_id == 0U ||
        !reader.read_hash(result.block_identifier) || !reader.exhausted())
        return std::nullopt;
    result.code = static_cast<MiningResultCode>(code);
    const bool identifier_is_zero = result.block_identifier == Hash256{};
    if (result.code != MiningResultCode::accepted && !identifier_is_zero)
        return std::nullopt;
    return result;
}

} // namespace onuros
