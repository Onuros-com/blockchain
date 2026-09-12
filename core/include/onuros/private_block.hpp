#pragma once

#include "onuros/private_admission.hpp"
#include "onuros/private_reward.hpp"

#include <optional>
#include <vector>

namespace onuros {

class ShieldedRootCalculator {
public:
    virtual ~ShieldedRootCalculator() = default;
    virtual std::optional<Hash256> calculate(
        const std::vector<Hash256>& active_commitments,
        const std::vector<Hash256>& new_commitments) const = 0;
};

enum class PrivateBlockError {
    none,
    invalid_transaction_root,
    missing_reward,
    malformed_reward,
    admission_failed,
    root_backend_unavailable,
    invalid_shielded_root,
    invalid_reward
};

struct PreparedPrivateBlock {
    PrivateBlockError error = PrivateBlockError::none;
    PrivateAdmissionError admission_error = PrivateAdmissionError::none;
    PrivateProofError proof_error = PrivateProofError::none;
    PrivateRewardError reward_error = PrivateRewardError::none;
    std::optional<PrivateBlockAdmission::Prepared> prepared;

    bool accepted() const {
        return error == PrivateBlockError::none && prepared.has_value();
    }
};

class PrivateBlockValidator {
    static PreparedPrivateBlock finish(
            const ShieldedState& state, const Block& block,
            const ShieldedRootCalculator& root_calculator,
            const PrivateRewardPolicy& reward_policy,
            PrivateBlockAdmission::Outcome admission) {
        if (!admission.accepted()) {
            PreparedPrivateBlock result;
            result.error = PrivateBlockError::admission_failed;
            result.admission_error = admission.error;
            result.proof_error = admission.proof_error;
            return result;
        }

        const auto resulting_root = root_calculator.calculate(
            state.ordered_commitments(), admission.prepared->commitments());
        if (!resulting_root)
            return {PrivateBlockError::root_backend_unavailable,
                    PrivateAdmissionError::none, PrivateProofError::none,
                    PrivateRewardError::none, std::nullopt};
        if (*resulting_root != block.header.shielded_root)
            return {PrivateBlockError::invalid_shielded_root,
                    PrivateAdmissionError::none, PrivateProofError::none,
                    PrivateRewardError::none, std::nullopt};

        const auto reward_error = reward_policy.validate(
            block.header.height, *admission.prepared,
            block.transactions.front());
        if (reward_error != PrivateRewardError::none) {
            PreparedPrivateBlock result;
            result.error = PrivateBlockError::invalid_reward;
            result.reward_error = reward_error;
            return result;
        }
        PreparedPrivateBlock result;
        result.prepared = std::move(admission.prepared);
        return result;
    }

public:
    static PreparedPrivateBlock prepare(
            const ShieldedState& state, const Block& block,
            const PrivateTransactionVerifier& verifier,
            const ShieldedRootCalculator& root_calculator,
            const PrivateRewardPolicy& reward_policy,
            const PrivateAdmissionLimits& limits) {
        if (!has_valid_transaction_root(block))
            return {PrivateBlockError::invalid_transaction_root,
                    PrivateAdmissionError::none, PrivateProofError::none,
                    PrivateRewardError::none, std::nullopt};
        if (block.transactions.empty())
            return {PrivateBlockError::missing_reward,
                    PrivateAdmissionError::none, PrivateProofError::none,
                    PrivateRewardError::none, std::nullopt};
        if (!decode_private_reward(block.transactions.front()).accepted())
            return {PrivateBlockError::malformed_reward,
                    PrivateAdmissionError::none, PrivateProofError::none,
                    PrivateRewardError::none, std::nullopt};

        const std::vector<TransactionEnvelope> private_transactions(
            block.transactions.begin() + 1, block.transactions.end());
        return finish(state, block, root_calculator, reward_policy,
            PrivateBlockAdmission::prepare(
            state, block.header.previous, private_transactions, verifier,
            limits));
    }

    static PreparedPrivateBlock prepare_parallel(
            const ShieldedState& state, const Block& block,
            const PrivateTransactionVerifier& verifier,
            const ShieldedRootCalculator& root_calculator,
            const PrivateRewardPolicy& reward_policy,
            const PrivateAdmissionLimits& limits, std::size_t workers) {
        if (!has_valid_transaction_root(block))
            return {PrivateBlockError::invalid_transaction_root,
                    PrivateAdmissionError::none, PrivateProofError::none,
                    PrivateRewardError::none, std::nullopt};
        if (block.transactions.empty())
            return {PrivateBlockError::missing_reward,
                    PrivateAdmissionError::none, PrivateProofError::none,
                    PrivateRewardError::none, std::nullopt};
        if (!decode_private_reward(block.transactions.front()).accepted())
            return {PrivateBlockError::malformed_reward,
                    PrivateAdmissionError::none, PrivateProofError::none,
                    PrivateRewardError::none, std::nullopt};
        const std::vector<TransactionEnvelope> private_transactions(
            block.transactions.begin() + 1, block.transactions.end());
        return finish(state, block, root_calculator, reward_policy,
            PrivateBlockAdmission::prepare_parallel(
                state, block.header.previous, private_transactions, verifier,
                limits, workers));
    }
};

} // namespace onuros
