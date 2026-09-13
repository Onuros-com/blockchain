#include "onuros/recursive_proof_gate.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace {

using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

bool contains(const RecursiveProofGateResult& result,
              RecursiveProofGateFailure failure) {
    return std::find(result.failures.begin(), result.failures.end(), failure) !=
           result.failures.end();
}

RecursiveProofMeasurements passing() {
    RecursiveProofMeasurements result;
    result.transfers = recursive_gate_target_transfers;
    result.effect_payload_bytes = 3'894'000U;
    result.proof_bytes = 1'024U;
    result.block_overhead_bytes = 4'096U;
    result.prover_p99_ms = recursive_gate_prover_p99_limit_ms - 1U;
    result.verifier_ms = recursive_gate_verifier_limit_ms - 1U;
    result.propagation_p99_ms =
        recursive_gate_propagation_p99_limit_ms - 1U;
    result.peak_vram_mib = recursive_gate_rtx3060_vram_limit_mib;
    result.deterministic = true;
    result.binds_parent_and_pow_block = true;
    result.binds_ordered_effects_and_authorizations = true;
    result.binds_old_and_new_state_roots = true;
    result.binds_nullifiers_values_fees_and_reward = true;
    result.rejects_corrupted_proof = true;
    result.rejects_withheld_proof = true;
    return result;
}

} // namespace

int main() {
    const auto accepted = evaluate_recursive_proof_gate(passing());
    check(accepted.passed());
    check(accepted.canonical_block_bytes == 3'899'120U);

    auto candidate = passing();
    candidate.transfers = recursive_gate_target_transfers - 1U;
    check(contains(evaluate_recursive_proof_gate(candidate),
                   RecursiveProofGateFailure::wrong_transfer_count));
    candidate = passing();
    candidate.effect_payload_bytes = recursive_gate_block_limit_bytes;
    check(contains(evaluate_recursive_proof_gate(candidate),
                   RecursiveProofGateFailure::block_too_large));
    candidate = passing();
    candidate.effect_payload_bytes =
        std::numeric_limits<std::uint64_t>::max();
    check(contains(evaluate_recursive_proof_gate(candidate),
                   RecursiveProofGateFailure::size_overflow));
    candidate = passing();
    candidate.prover_p99_ms = recursive_gate_prover_p99_limit_ms;
    check(contains(evaluate_recursive_proof_gate(candidate),
                   RecursiveProofGateFailure::prover_too_slow));
    candidate = passing();
    candidate.verifier_ms = recursive_gate_verifier_limit_ms;
    check(contains(evaluate_recursive_proof_gate(candidate),
                   RecursiveProofGateFailure::verifier_too_slow));
    candidate = passing();
    candidate.propagation_p99_ms =
        recursive_gate_propagation_p99_limit_ms;
    check(contains(evaluate_recursive_proof_gate(candidate),
                   RecursiveProofGateFailure::propagation_too_slow));
    candidate = passing();
    candidate.peak_vram_mib = recursive_gate_rtx3060_vram_limit_mib + 1U;
    check(contains(evaluate_recursive_proof_gate(candidate),
                   RecursiveProofGateFailure::vram_too_high));
    candidate = passing();
    candidate.deterministic = false;
    check(contains(evaluate_recursive_proof_gate(candidate),
                   RecursiveProofGateFailure::nondeterministic));
    candidate = passing();
    candidate.binds_parent_and_pow_block = false;
    check(contains(evaluate_recursive_proof_gate(candidate),
                   RecursiveProofGateFailure::missing_parent_or_pow_binding));
    candidate = passing();
    candidate.binds_ordered_effects_and_authorizations = false;
    check(contains(
        evaluate_recursive_proof_gate(candidate),
        RecursiveProofGateFailure::missing_effect_or_authorization_binding));
    candidate = passing();
    candidate.binds_old_and_new_state_roots = false;
    check(contains(evaluate_recursive_proof_gate(candidate),
                   RecursiveProofGateFailure::missing_state_root_binding));
    candidate = passing();
    candidate.binds_nullifiers_values_fees_and_reward = false;
    check(contains(evaluate_recursive_proof_gate(candidate),
                   RecursiveProofGateFailure::missing_value_or_reward_binding));
    candidate = passing();
    candidate.rejects_corrupted_proof = false;
    check(contains(evaluate_recursive_proof_gate(candidate),
                   RecursiveProofGateFailure::corrupted_proof_accepted));
    candidate = passing();
    candidate.rejects_withheld_proof = false;
    check(contains(evaluate_recursive_proof_gate(candidate),
                   RecursiveProofGateFailure::withheld_proof_accepted));
    return 0;
}
