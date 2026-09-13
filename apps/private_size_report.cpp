#include "onuros/consensus_limits.hpp"
#include "onuros/private_transaction_size.hpp"

#include <cstddef>
#include <iomanip>
#include <iostream>

namespace {

using namespace onuros;

void report(std::size_t actions) {
    const auto proof = orchard_proof_base_size +
                       orchard_proof_per_action_size * actions;
    const auto size = private_transaction_size(actions, proof);
    if (!size) return;
    std::cout << "actions=" << actions
              << " outer_framing=" << size->outer_framing
              << " bundle_prefix=" << size->bundle_prefix
              << " effects=" << size->action_effects
              << " ciphertext=" << size->ciphertext
              << " action_signatures=" << size->action_authorization
              << " proof_framing=" << size->proof_framing
              << " proof=" << size->proof
              << " binding_signature=" << size->binding_signature
              << " body=" << size->body()
              << " transaction=" << size->transaction() << '\n';
}

} // namespace

int main() {
    report(1U);
    report(2U);

    const auto two = private_transaction_size(
        2U, orchard_proof_base_size + 2U * orchard_proof_per_action_size);
    const auto projection = two
        ? project_private_block_size(100U, 60U, two->transaction())
        : std::nullopt;
    if (!projection) return 1;
    const auto privacy_floor = project_private_block_size(
        100U, 60U, two->effect_and_ciphertext_transaction());
    if (!privacy_floor) return 1;

    constexpr auto target = 4U * bytes_per_mib;
    const auto gross_budget =
        (target - block_prefix_encoded_size) / projection->transaction_count;
    const auto reduction = 100.0 *
        (1.0 - static_cast<double>(gross_budget) /
                   static_cast<double>(two->transaction()));
    std::cout << "target_tps=100 interval_seconds=60 transactions="
              << projection->transaction_count
              << " current_block_bytes=" << projection->serialized_block_bytes
              << " current_block_mib=" << std::fixed << std::setprecision(2)
              << static_cast<double>(projection->serialized_block_bytes) /
                     static_cast<double>(bytes_per_mib)
              << " target_block_bytes=" << target
              << " gross_bytes_per_transaction_budget=" << gross_budget
              << " required_reduction_percent=" << reduction << '\n';
    std::cout << "current_two_action_effect_and_ciphertext_bytes="
              << two->effect_and_ciphertext_transaction()
              << " current_privacy_payload_floor_bytes="
              << privacy_floor->serialized_block_bytes
              << " current_privacy_payload_floor_mib=" << std::fixed
              << std::setprecision(2)
              << static_cast<double>(privacy_floor->serialized_block_bytes) /
                     static_cast<double>(bytes_per_mib)
              << " four_mib_feasible_with_proof_aggregation_only=false\n";
    return 0;
}
