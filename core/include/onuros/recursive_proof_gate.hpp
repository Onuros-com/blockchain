#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace onuros {

inline constexpr std::uint64_t recursive_gate_target_transfers = 6'000U;
inline constexpr std::uint64_t recursive_gate_block_limit_bytes =
    16U * 1024U * 1024U;
inline constexpr std::uint64_t recursive_gate_prover_p99_limit_ms = 45'000U;
inline constexpr std::uint64_t recursive_gate_propagation_p99_limit_ms = 15'000U;
inline constexpr std::uint64_t recursive_gate_verifier_limit_ms = 60'000U;
inline constexpr std::uint64_t recursive_gate_rtx3060_vram_limit_mib = 12'288U;

struct RecursiveProofMeasurements {
    std::uint64_t transfers = 0U;
    std::uint64_t effect_payload_bytes = 0U;
    std::uint64_t proof_bytes = 0U;
    std::uint64_t block_overhead_bytes = 0U;
    std::uint64_t prover_p99_ms = 0U;
    std::uint64_t verifier_ms = 0U;
    std::uint64_t propagation_p99_ms = 0U;
    std::uint64_t peak_vram_mib = 0U;
    bool deterministic = false;
    bool binds_parent_and_pow_block = false;
    bool binds_ordered_effects_and_authorizations = false;
    bool binds_old_and_new_state_roots = false;
    bool binds_nullifiers_values_fees_and_reward = false;
    bool rejects_corrupted_proof = false;
    bool rejects_withheld_proof = false;
};

enum class RecursiveProofGateFailure {
    wrong_transfer_count,
    empty_effect_payload,
    empty_proof,
    size_overflow,
    block_too_large,
    prover_too_slow,
    verifier_too_slow,
    propagation_too_slow,
    vram_too_high,
    nondeterministic,
    missing_parent_or_pow_binding,
    missing_effect_or_authorization_binding,
    missing_state_root_binding,
    missing_value_or_reward_binding,
    corrupted_proof_accepted,
    withheld_proof_accepted
};

struct RecursiveProofGateResult {
    std::uint64_t canonical_block_bytes = 0U;
    std::vector<RecursiveProofGateFailure> failures;

    bool passed() const { return failures.empty(); }
};

inline RecursiveProofGateResult evaluate_recursive_proof_gate(
        const RecursiveProofMeasurements& measurement) {
    RecursiveProofGateResult result;
    if (measurement.transfers != recursive_gate_target_transfers)
        result.failures.push_back(
            RecursiveProofGateFailure::wrong_transfer_count);
    if (measurement.effect_payload_bytes == 0U)
        result.failures.push_back(
            RecursiveProofGateFailure::empty_effect_payload);
    if (measurement.proof_bytes == 0U)
        result.failures.push_back(RecursiveProofGateFailure::empty_proof);

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (measurement.effect_payload_bytes >
            maximum - measurement.proof_bytes ||
        measurement.effect_payload_bytes + measurement.proof_bytes >
            maximum - measurement.block_overhead_bytes) {
        result.failures.push_back(RecursiveProofGateFailure::size_overflow);
    } else {
        result.canonical_block_bytes = measurement.effect_payload_bytes +
                                       measurement.proof_bytes +
                                       measurement.block_overhead_bytes;
        if (result.canonical_block_bytes > recursive_gate_block_limit_bytes)
            result.failures.push_back(
                RecursiveProofGateFailure::block_too_large);
    }
    if (measurement.prover_p99_ms == 0U ||
        measurement.prover_p99_ms >= recursive_gate_prover_p99_limit_ms)
        result.failures.push_back(RecursiveProofGateFailure::prover_too_slow);
    if (measurement.verifier_ms == 0U ||
        measurement.verifier_ms >= recursive_gate_verifier_limit_ms)
        result.failures.push_back(RecursiveProofGateFailure::verifier_too_slow);
    if (measurement.propagation_p99_ms == 0U ||
        measurement.propagation_p99_ms >=
            recursive_gate_propagation_p99_limit_ms)
        result.failures.push_back(
            RecursiveProofGateFailure::propagation_too_slow);
    if (measurement.peak_vram_mib == 0U ||
        measurement.peak_vram_mib > recursive_gate_rtx3060_vram_limit_mib)
        result.failures.push_back(RecursiveProofGateFailure::vram_too_high);
    if (!measurement.deterministic)
        result.failures.push_back(RecursiveProofGateFailure::nondeterministic);
    if (!measurement.binds_parent_and_pow_block)
        result.failures.push_back(
            RecursiveProofGateFailure::missing_parent_or_pow_binding);
    if (!measurement.binds_ordered_effects_and_authorizations)
        result.failures.push_back(
            RecursiveProofGateFailure::missing_effect_or_authorization_binding);
    if (!measurement.binds_old_and_new_state_roots)
        result.failures.push_back(
            RecursiveProofGateFailure::missing_state_root_binding);
    if (!measurement.binds_nullifiers_values_fees_and_reward)
        result.failures.push_back(
            RecursiveProofGateFailure::missing_value_or_reward_binding);
    if (!measurement.rejects_corrupted_proof)
        result.failures.push_back(
            RecursiveProofGateFailure::corrupted_proof_accepted);
    if (!measurement.rejects_withheld_proof)
        result.failures.push_back(
            RecursiveProofGateFailure::withheld_proof_accepted);
    return result;
}

} // namespace onuros
