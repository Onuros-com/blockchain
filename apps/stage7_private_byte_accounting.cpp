#include "onuros/consensus_limits.hpp"
#include "onuros/onuros_privacy_engine_backend.hpp"
#include "onuros/private_reward.hpp"

#include <cstddef>
#include <iostream>
#include <limits>

int main() {
    constexpr std::size_t transfers = 6'000U;
    constexpr std::size_t envelope_overhead = 8U;
    constexpr std::size_t batch_count = 4U;
    constexpr std::size_t payment_envelope =
        onuros::onuros_private_payment_bytes + envelope_overhead;
    constexpr std::size_t reward_body = 4U + 4U + 24U + 96U;
    constexpr std::size_t reward_envelope = reward_body + envelope_overhead;
    constexpr std::size_t block_fixed = onuros::block_header_encoded_size + 4U;
    constexpr std::size_t reserve = 64U * 1024U;
    constexpr std::size_t usable = onuros::max_serialized_block_bytes -
        block_fixed - reward_envelope - reserve;
    constexpr std::size_t maximum_payments = usable / payment_envelope;
    std::cout << "proof_system=groth16-bls12-381\n"
              << "commitment_hash=poseidon\n"
              << "payment_body_bytes="
              << onuros::onuros_private_payment_bytes << '\n'
              << "transaction_envelope_overhead_bytes="
              << envelope_overhead << '\n'
              << "payment_envelope_bytes=" << payment_envelope << '\n'
              << "network_batch_count_bytes=" << batch_count << '\n'
              << "single_payment_batch_bytes="
              << batch_count + payment_envelope << '\n'
              << "payment_6000_body_bytes="
              << transfers * onuros::onuros_private_payment_bytes << '\n'
              << "payment_6000_envelope_bytes="
              << transfers * payment_envelope << '\n'
              << "payment_6000_network_batch_bytes="
              << batch_count + transfers * payment_envelope << '\n'
              << "block_fixed_bytes=" << block_fixed << '\n'
              << "reward_envelope_bytes=" << reward_envelope << '\n'
              << "reserved_bytes=" << reserve << '\n'
              << "maximum_payments_with_reserve=" << maximum_payments << '\n'
              << "maximum_tps_at_60_seconds="
              << static_cast<double>(maximum_payments) / 60.0 << '\n';
    return 0;
}
