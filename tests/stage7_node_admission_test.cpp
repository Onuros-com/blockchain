#include "onuros/stage7_node_admission.hpp"

#include <cstdlib>

namespace {
using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

Hash256 value(std::uint32_t number) {
    Hash256 result{};
    for (std::size_t i = 0U; i < 4U; ++i)
        result[result.size() - 1U - i] =
            static_cast<std::uint8_t>(number >> (i * 8U));
    return result;
}

TransactionEnvelope transaction(std::uint8_t selector,
                                std::uint8_t nullifier = 0U) {
    return {2U, {selector, nullifier == 0U ? selector : nullifier}};
}

class Verifier final : public PrivateTransactionVerifier {
    Hash256 anchor_;

public:
    explicit Verifier(Hash256 anchor) : anchor_(anchor) {}

    VerifiedPrivateEffects verify(
            const TransactionEnvelope& candidate) const override {
        if (candidate.body.size() != 2U || candidate.body[0] == 0U)
            return {PrivateProofError::invalid_proof, {}, {}, {}, 0};
        return {PrivateProofError::none, anchor_,
                {value(100U + candidate.body[1])},
                {value(200U + candidate.body[0])}, 1};
    }
};

PrivateMempoolLimits mempool_limits() {
    return {8U, 4096U, 8U, 64U, 2U};
}
} // namespace

int main() {
    const auto genesis = value(1U);
    const auto root = value(2U);
    ShieldedState state(genesis, root);
    Verifier verifier(root);
    PrivateMempool mempool(mempool_limits());
    ValidatedRelayPool relay(1U, 4096U);

    std::size_t block_calls = 0U;
    Stage7NodeAdmission admission(
        state, verifier, mempool, relay,
        [&block_calls](const Block& block, std::uint64_t now) {
            ++block_calls;
            return now == 500U && !block.transactions.empty();
        });

    const auto first = transaction(1U);
    check(admission.admit_transaction(first).accepted());
    check(mempool.size() == 1U && relay.size() == 1U);

    const auto duplicate = admission.admit_transaction(first);
    check(duplicate.error ==
              NetworkTransactionAdmissionError::mempool_rejected &&
          duplicate.mempool_result.error ==
              PrivateMempoolError::duplicate_transaction);
    check(mempool.size() == 1U && relay.size() == 1U);

    const auto invalid = admission.admit_transaction(transaction(0U));
    check(invalid.error ==
              NetworkTransactionAdmissionError::mempool_rejected &&
          invalid.mempool_result.error ==
              PrivateMempoolError::verification_failed &&
          invalid.mempool_result.proof_error ==
              PrivateProofError::invalid_proof);
    check(mempool.size() == 1U && relay.size() == 1U);

    const auto conflict = admission.admit_transaction(transaction(3U, 1U));
    check(conflict.error ==
              NetworkTransactionAdmissionError::mempool_rejected &&
          conflict.mempool_result.error ==
              PrivateMempoolError::conflicting_nullifier);
    check(mempool.size() == 1U && relay.size() == 1U);

    const auto relay_full = admission.admit_transaction(transaction(2U));
    check(relay_full.error ==
          NetworkTransactionAdmissionError::relay_rejected);
    check(mempool.size() == 1U && relay.size() == 1U);
    const auto selected = mempool.select(8U, 4096U, 8U);
    check(selected.size() == 1U &&
          transaction_id(selected.front()) == transaction_id(first));

    Block rejected_block;
    check(admission.admit_block(rejected_block, 500U) ==
          NetworkBlockAdmissionError::rejected);
    Block accepted_block;
    accepted_block.transactions.push_back(first);
    check(admission.admit_block(accepted_block, 500U) ==
          NetworkBlockAdmissionError::none);
    check(block_calls == 2U);

    Stage7NodeAdmission unavailable(state, verifier, mempool, relay);
    check(unavailable.admit_block(accepted_block, 500U) ==
          NetworkBlockAdmissionError::unavailable);

    const auto& metrics = admission.metrics();
    check(metrics.transactions_accepted == 1U);
    check(metrics.transactions_mempool_rejected == 3U);
    check(metrics.transactions_relay_rejected == 1U);
    check(metrics.blocks_accepted == 1U);
    check(metrics.blocks_rejected == 1U);
    return 0;
}
