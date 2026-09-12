#include "onuros/private_admission.hpp"

#include <cstdlib>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

Hash256 value(std::uint8_t byte) {
    Hash256 hash{};
    hash.back() = byte;
    return hash;
}

TransactionEnvelope transaction(std::uint8_t selector) {
    return TransactionEnvelope{1U, {selector}};
}

class ScriptedVerifier final : public PrivateTransactionVerifier {
    Hash256 anchor_;
public:
    explicit ScriptedVerifier(Hash256 anchor) : anchor_(anchor) {}

    VerifiedPrivateEffects verify(
            const TransactionEnvelope& candidate) const override {
        if (candidate.body.empty())
            return {PrivateProofError::malformed_encoding, {}, {}, {}, 0};
        switch (candidate.body.front()) {
        case 1: return {PrivateProofError::none, anchor_, {value(20)}, {value(30)}, 10};
        case 2: return {PrivateProofError::none, anchor_, {value(21)}, {value(31)}, 20};
        case 3: return {PrivateProofError::none, anchor_, {value(20)}, {value(32)}, 1};
        case 4: return {PrivateProofError::invalid_proof, anchor_, {}, {}, 0};
        case 5: return {PrivateProofError::none, anchor_, {value(22)}, {value(30)}, 1};
        case 6: return {PrivateProofError::none, value(99), {value(23)}, {value(33)}, 1};
        case 7: return {PrivateProofError::none, anchor_, {}, {value(34)}, 1};
        case 8: return {PrivateProofError::none, anchor_, {value(24)}, {}, 1};
        case 9: return {PrivateProofError::none, anchor_, {value(25)}, {value(35)}, -1};
        case 10:
            return {PrivateProofError::none, anchor_, {value(26)}, {value(36)},
                    std::numeric_limits<Amount>::max()};
        case 11: return {PrivateProofError::none, anchor_, {value(27)}, {value(37)}, 1};
        case 12:
            return {PrivateProofError::none, anchor_,
                    {value(40), value(41), value(42)}, {value(43)}, 1};
        case 13:
            return {PrivateProofError::none, anchor_, {value(44)},
                    {value(45), value(46), value(47)}, 1};
        default:
            return {PrivateProofError::unsupported_proof_version, {}, {}, {}, 0};
        }
    }
};

PrivateAdmissionLimits limits() {
    return {1024U, 4U, 4U, 8U};
}

PrivateBlockAdmission::Outcome prepare(
        const ShieldedState& state, const ScriptedVerifier& verifier,
        std::vector<TransactionEnvelope> transactions,
        PrivateAdmissionLimits admission_limits = limits()) {
    return PrivateBlockAdmission::prepare(
        state, state.tip(), transactions, verifier, admission_limits);
}

void expect_error(const ShieldedState& state, const ScriptedVerifier& verifier,
                  std::vector<TransactionEnvelope> transactions,
                  PrivateAdmissionError expected,
                  PrivateAdmissionLimits admission_limits = limits()) {
    const auto result =
        prepare(state, verifier, std::move(transactions), admission_limits);
    check(!result.accepted());
    check(result.error == expected);
    check(!result.prepared.has_value());
}

} // namespace

int main() {
    const auto genesis = value(1);
    const auto initial_root = value(10);
    const auto block_one = value(2);
    const auto block_two = value(3);
    const auto root_one = value(11);
    const ScriptedVerifier verifier(initial_root);

    {
        ShieldedState state(genesis, initial_root);
        const auto wrong_parent = PrivateBlockAdmission::prepare(
            state, value(77), {transaction(1)}, verifier, limits());
        check(wrong_parent.error == PrivateAdmissionError::wrong_parent);

        const auto valid =
            prepare(state, verifier, {transaction(1), transaction(2)});
        check(valid.accepted());
        check(valid.prepared->transaction_ids().size() == 2U);
        check(valid.prepared->nullifiers().size() == 2U);
        check(valid.prepared->commitments().size() == 2U);
        check(valid.prepared->fees() == 30);

        const auto parallel = PrivateBlockAdmission::prepare_parallel(
            state, state.tip(), {transaction(1), transaction(2)}, verifier,
            limits(), 2U);
        check(parallel.accepted());
        check(parallel.prepared->transaction_ids() ==
              valid.prepared->transaction_ids());
        check(parallel.prepared->nullifiers() ==
              valid.prepared->nullifiers());
        check(parallel.prepared->commitments() ==
              valid.prepared->commitments());
        check(parallel.prepared->fees() == valid.prepared->fees());

        const auto parallel_invalid = PrivateBlockAdmission::prepare_parallel(
            state, state.tip(), {transaction(4), transaction(6)}, verifier,
            limits(), 2U);
        check(parallel_invalid.error ==
              PrivateAdmissionError::proof_verification_failed);
        check(parallel_invalid.proof_error == PrivateProofError::invalid_proof);
    }

    {
        ShieldedState state(genesis, initial_root);
        const auto first = prepare(state, verifier, {transaction(1)});
        check(first.accepted());
        check(state.connect(block_one, root_one, *first.prepared) ==
               PrivateAdmissionError::none);
        check(state.tip() == block_one);
        check(state.root() == root_one);
        check(state.height() == 1U);
        check(state.spent(value(20)));
        check(state.contains_commitment(value(30)));

        expect_error(state, verifier, {transaction(3)},
                     PrivateAdmissionError::repeated_nullifier);
        expect_error(state, verifier, {transaction(5)},
                     PrivateAdmissionError::repeated_commitment);

        check(state.disconnect(block_two) ==
               PrivateAdmissionError::disconnect_order_mismatch);
        check(state.tip() == block_one);
        check(state.spent_count() == 1U);
        check(state.disconnect(block_one) == PrivateAdmissionError::none);
        check(state.tip() == genesis);
        check(state.root() == initial_root);
        check(state.spent_count() == 0U);
        check(state.commitment_count() == 0U);
        check(state.disconnect(genesis) ==
               PrivateAdmissionError::disconnect_past_genesis);
    }

    {
        ShieldedState state(genesis, initial_root);
        const auto first = prepare(state, verifier, {transaction(1)});
        const auto stale = prepare(state, verifier, {transaction(2)});
        check(first.accepted() && stale.accepted());
        check(state.connect(block_one, root_one, *first.prepared) ==
               PrivateAdmissionError::none);
        check(state.connect(block_two, value(12), *stale.prepared) ==
               PrivateAdmissionError::stale_preparation);

        const auto second = prepare(state, verifier, {transaction(2)});
        check(second.accepted());
        check(state.connect(block_one, value(12), *second.prepared) ==
               PrivateAdmissionError::duplicate_block);
        check(state.connect(block_two, value(12), *second.prepared) ==
               PrivateAdmissionError::none);
        check(state.height() == 2U);
    }

    {
        ShieldedState state(genesis, initial_root);
        expect_error(state, verifier, {transaction(1), transaction(1)},
                     PrivateAdmissionError::duplicate_transaction);
        expect_error(state, verifier, {transaction(6)},
                     PrivateAdmissionError::unknown_anchor);
        expect_error(state, verifier, {transaction(7)},
                     PrivateAdmissionError::empty_spend);
        expect_error(state, verifier, {transaction(8)},
                     PrivateAdmissionError::empty_output);
        expect_error(state, verifier, {transaction(9)},
                     PrivateAdmissionError::negative_fee);

        const auto invalid = prepare(state, verifier, {transaction(4)});
        check(invalid.error ==
               PrivateAdmissionError::proof_verification_failed);
        check(invalid.proof_error == PrivateProofError::invalid_proof);

        auto small = limits();
        small.max_transaction_body_bytes = 0U;
        expect_error(state, verifier, {transaction(1)},
                     PrivateAdmissionError::transaction_body_too_large, small);

        small = limits();
        small.max_nullifiers_per_transaction = 2U;
        expect_error(state, verifier, {transaction(12)},
                     PrivateAdmissionError::too_many_nullifiers, small);

        small = limits();
        small.max_commitments_per_transaction = 2U;
        expect_error(state, verifier, {transaction(13)},
                     PrivateAdmissionError::too_many_commitments, small);

        small = limits();
        small.max_private_actions_per_block = 2U;
        expect_error(state, verifier, {transaction(12)},
                     PrivateAdmissionError::too_many_private_actions, small);

        expect_error(state, verifier, {transaction(10), transaction(11)},
                     PrivateAdmissionError::fee_overflow);
    }

    return 0;
}
