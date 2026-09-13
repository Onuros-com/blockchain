#pragma once

#include "onuros/recursive_proof_gate.hpp"

#include <charconv>
#include <cstdint>
#include <istream>
#include <map>
#include <set>
#include <string>
#include <system_error>

namespace onuros {

inline constexpr std::string_view recursive_proof_manifest_format =
    "onuros-stage7-recursive-proof-v1";

struct RecursiveProofManifestResult {
    std::string error;
    std::string backend;
    std::string backend_commit;
    std::string proof_hash_run_1;
    std::string proof_hash_run_2;
    std::string raw_log_sha256;
    RecursiveProofMeasurements measurements;

    bool accepted() const { return error.empty(); }
};

namespace recursive_manifest_detail {

inline bool is_sha256(const std::string& value) {
    if (value.size() != 64U) return false;
    for (const auto character : value) {
        const bool digit = character >= '0' && character <= '9';
        const bool lower = character >= 'a' && character <= 'f';
        if (!digit && !lower) return false;
    }
    return true;
}

inline bool parse_u64(const std::string& value, std::uint64_t& output) {
    if (value.empty()) return false;
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), output);
    return parsed.ec == std::errc{} &&
           parsed.ptr == value.data() + value.size();
}

inline bool parse_bool(const std::string& value, bool& output) {
    if (value == "true") {
        output = true;
        return true;
    }
    if (value == "false") {
        output = false;
        return true;
    }
    return false;
}

} // namespace recursive_manifest_detail

inline RecursiveProofManifestResult parse_recursive_proof_manifest(
        std::istream& input) {
    RecursiveProofManifestResult result;
    std::map<std::string, std::string> fields;
    std::string line;
    std::size_t line_number = 0U;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty()) continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0U ||
            separator + 1U == line.size() ||
            line.find('=', separator + 1U) != std::string::npos) {
            result.error = "malformed line " + std::to_string(line_number);
            return result;
        }
        const auto key = line.substr(0U, separator);
        const auto value = line.substr(separator + 1U);
        if (!fields.emplace(key, value).second) {
            result.error = "duplicate field: " + key;
            return result;
        }
    }

    const std::set<std::string> required{
        "format", "backend", "backend_commit", "proof_hash_run_1",
        "proof_hash_run_2", "raw_log_sha256", "transfers",
        "verified_onp2_transfers", "effect_payload_bytes", "proof_bytes",
        "block_overhead_bytes", "prover_p99_ms", "verifier_ms",
        "propagation_p99_ms", "peak_vram_mib", "binds_parent_and_pow_block",
        "binds_ordered_effects_and_authorizations",
        "binds_old_and_new_state_roots",
        "binds_nullifiers_values_fees_and_reward", "rejects_corrupted_proof",
        "rejects_withheld_proof", "rejects_malformed_proof",
        "recovers_after_interruption"};
    for (const auto& [key, value] : fields) {
        static_cast<void>(value);
        if (required.count(key) == 0U) {
            result.error = "unknown field: " + key;
            return result;
        }
    }
    for (const auto& key : required) {
        if (fields.count(key) == 0U) {
            result.error = "missing field: " + key;
            return result;
        }
    }
    if (fields["format"] != recursive_proof_manifest_format) {
        result.error = "unsupported manifest format";
        return result;
    }

    result.backend = fields["backend"];
    result.backend_commit = fields["backend_commit"];
    result.proof_hash_run_1 = fields["proof_hash_run_1"];
    result.proof_hash_run_2 = fields["proof_hash_run_2"];
    result.raw_log_sha256 = fields["raw_log_sha256"];
    if (result.backend.empty() || result.backend == "none") {
        result.measurements.recursive_backend = false;
    } else {
        result.measurements.recursive_backend = true;
    }
    if (!recursive_manifest_detail::is_sha256(result.backend_commit) ||
        !recursive_manifest_detail::is_sha256(result.proof_hash_run_1) ||
        !recursive_manifest_detail::is_sha256(result.proof_hash_run_2) ||
        !recursive_manifest_detail::is_sha256(result.raw_log_sha256)) {
        result.error = "hash fields must be lowercase SHA-256 values";
        return result;
    }
    result.measurements.deterministic =
        result.proof_hash_run_1 == result.proof_hash_run_2;

    const std::pair<const char*, std::uint64_t*> numbers[]{
        {"transfers", &result.measurements.transfers},
        {"verified_onp2_transfers",
         &result.measurements.verified_onp2_transfers},
        {"effect_payload_bytes", &result.measurements.effect_payload_bytes},
        {"proof_bytes", &result.measurements.proof_bytes},
        {"block_overhead_bytes", &result.measurements.block_overhead_bytes},
        {"prover_p99_ms", &result.measurements.prover_p99_ms},
        {"verifier_ms", &result.measurements.verifier_ms},
        {"propagation_p99_ms", &result.measurements.propagation_p99_ms},
        {"peak_vram_mib", &result.measurements.peak_vram_mib}};
    for (const auto& [key, output] : numbers) {
        if (!recursive_manifest_detail::parse_u64(fields[key], *output)) {
            result.error = std::string{"invalid unsigned integer: "} + key;
            return result;
        }
    }

    const std::pair<const char*, bool*> booleans[]{
        {"binds_parent_and_pow_block",
         &result.measurements.binds_parent_and_pow_block},
        {"binds_ordered_effects_and_authorizations",
         &result.measurements.binds_ordered_effects_and_authorizations},
        {"binds_old_and_new_state_roots",
         &result.measurements.binds_old_and_new_state_roots},
        {"binds_nullifiers_values_fees_and_reward",
         &result.measurements.binds_nullifiers_values_fees_and_reward},
        {"rejects_corrupted_proof",
         &result.measurements.rejects_corrupted_proof},
        {"rejects_withheld_proof",
         &result.measurements.rejects_withheld_proof},
        {"rejects_malformed_proof",
         &result.measurements.rejects_malformed_proof},
        {"recovers_after_interruption",
         &result.measurements.recovers_after_interruption}};
    for (const auto& [key, output] : booleans) {
        if (!recursive_manifest_detail::parse_bool(fields[key], *output)) {
            result.error = std::string{"invalid boolean: "} + key;
            return result;
        }
    }
    return result;
}

} // namespace onuros
