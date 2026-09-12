#pragma once

#include "onuros/block_format.hpp"
#include "onuros/economics.hpp"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace onuros {

struct PrivateAdmissionLimits {
    std::size_t max_transaction_body_bytes;
    std::size_t max_nullifiers_per_transaction;
    std::size_t max_commitments_per_transaction;
    std::size_t max_private_actions_per_block;
};

enum class PrivateProofError {
    none,
    malformed_encoding,
    invalid_proof,
    invalid_signature,
    invalid_balance,
    invalid_effect_binding,
    backend_unavailable,
    unsupported_proof_version
};

struct VerifiedPrivateEffects {
    PrivateProofError error = PrivateProofError::malformed_encoding;
    Hash256 anchor{};
    std::vector<Hash256> nullifiers;
    std::vector<Hash256> commitments;
    Amount fee = 0;
};

class PrivateTransactionVerifier {
public:
    virtual ~PrivateTransactionVerifier() = default;
    virtual VerifiedPrivateEffects verify(
        const TransactionEnvelope& transaction) const = 0;
};

enum class PrivateAdmissionError {
    none,
    wrong_parent,
    transaction_body_too_large,
    proof_verification_failed,
    empty_spend,
    empty_output,
    too_many_nullifiers,
    too_many_commitments,
    too_many_private_actions,
    unknown_anchor,
    duplicate_transaction,
    repeated_nullifier,
    repeated_commitment,
    negative_fee,
    fee_overflow,
    stale_preparation,
    duplicate_block,
    disconnect_past_genesis,
    disconnect_order_mismatch
};

struct ShieldedUndo {
    Hash256 block_id{};
    Hash256 parent_block{};
    Hash256 previous_root{};
    Hash256 resulting_root{};
    std::vector<Hash256> nullifiers;
    std::vector<Hash256> commitments;
};

struct ShieldedSnapshot {
    Hash256 genesis_block{};
    Hash256 genesis_root{};
    Hash256 tip_block{};
    Hash256 current_root{};
    std::vector<Hash256> nullifiers;
    std::vector<Hash256> commitments;
    std::vector<ShieldedUndo> history;
};

class ShieldedState;

class PrivateBlockAdmission {
public:
    class Prepared {
        friend class PrivateBlockAdmission;
        friend class ShieldedState;

        Hash256 base_tip_{};
        Hash256 base_root_{};
        std::vector<Hash256> transaction_ids_;
        std::vector<Hash256> nullifiers_;
        std::vector<Hash256> commitments_;
        Amount fees_ = 0;

        Prepared(Hash256 base_tip, Hash256 base_root,
                 std::vector<Hash256> transaction_ids,
                 std::vector<Hash256> nullifiers,
                 std::vector<Hash256> commitments, Amount fees)
            : base_tip_(base_tip), base_root_(base_root),
              transaction_ids_(std::move(transaction_ids)),
              nullifiers_(std::move(nullifiers)),
              commitments_(std::move(commitments)), fees_(fees) {}

    public:
        Prepared(const Prepared&) = default;
        Prepared(Prepared&&) noexcept = default;
        Prepared& operator=(const Prepared&) = default;
        Prepared& operator=(Prepared&&) noexcept = default;

        const std::vector<Hash256>& transaction_ids() const {
            return transaction_ids_;
        }
        const std::vector<Hash256>& nullifiers() const { return nullifiers_; }
        const std::vector<Hash256>& commitments() const { return commitments_; }
        Amount fees() const { return fees_; }
    };

    struct Outcome {
        PrivateAdmissionError error = PrivateAdmissionError::none;
        PrivateProofError proof_error = PrivateProofError::none;
        std::optional<Prepared> prepared;

        bool accepted() const {
            return error == PrivateAdmissionError::none && prepared.has_value();
        }
    };

private:
    static Outcome prepare_verified(
        const ShieldedState& state, const Hash256& parent,
        const std::vector<TransactionEnvelope>& transactions,
        std::vector<VerifiedPrivateEffects> effects,
        const PrivateAdmissionLimits& limits);

public:
    static Outcome prepare(const ShieldedState& state, const Hash256& parent,
                           const std::vector<TransactionEnvelope>& transactions,
                           const PrivateTransactionVerifier& verifier,
                           const PrivateAdmissionLimits& limits);

    static Outcome prepare_parallel(
        const ShieldedState& state, const Hash256& parent,
        const std::vector<TransactionEnvelope>& transactions,
        const PrivateTransactionVerifier& verifier,
        const PrivateAdmissionLimits& limits, std::size_t workers);
};

class ShieldedState {
    Hash256 genesis_block_{};
    Hash256 genesis_root_{};
    Hash256 tip_block_{};
    Hash256 current_root_{};
    std::set<Hash256> nullifiers_;
    std::set<Hash256> commitments_;
    std::map<Hash256, std::size_t> active_roots_;
    std::vector<ShieldedUndo> history_;

    static void erase_prefix(std::set<Hash256>& values,
                             const std::vector<Hash256>& inserted,
                             std::size_t count) noexcept {
        for (std::size_t i = 0; i < count; ++i) values.erase(inserted[i]);
    }

public:
    ShieldedState(Hash256 genesis_block, Hash256 initial_root)
        : genesis_block_(genesis_block), genesis_root_(initial_root),
          tip_block_(genesis_block),
          current_root_(initial_root) {
        active_roots_.emplace(initial_root, 1U);
    }

    const Hash256& tip() const { return tip_block_; }
    const Hash256& root() const { return current_root_; }
    bool has_anchor(const Hash256& root) const {
        return active_roots_.find(root) != active_roots_.end();
    }
    bool spent(const Hash256& nullifier) const {
        return nullifiers_.find(nullifier) != nullifiers_.end();
    }
    bool contains_commitment(const Hash256& commitment) const {
        return commitments_.find(commitment) != commitments_.end();
    }
    std::size_t height() const { return history_.size(); }
    std::size_t spent_count() const { return nullifiers_.size(); }
    std::size_t commitment_count() const { return commitments_.size(); }
    const std::vector<ShieldedUndo>& history() const { return history_; }

    std::vector<Hash256> ordered_commitments() const {
        std::vector<Hash256> ordered;
        ordered.reserve(commitments_.size());
        for (const auto& undo : history_)
            ordered.insert(ordered.end(), undo.commitments.begin(),
                           undo.commitments.end());
        return ordered;
    }

    ShieldedSnapshot snapshot() const {
        return {genesis_block_, genesis_root_, tip_block_, current_root_,
                {nullifiers_.begin(), nullifiers_.end()},
                {commitments_.begin(), commitments_.end()}, history_};
    }

    static std::optional<ShieldedState> restore(
            const ShieldedSnapshot& snapshot) {
        ShieldedState restored(snapshot.genesis_block, snapshot.genesis_root);
        Hash256 expected_parent = snapshot.genesis_block;
        Hash256 expected_root = snapshot.genesis_root;
        std::set<Hash256> expected_nullifiers;
        std::set<Hash256> expected_commitments;
        for (const auto& undo : snapshot.history) {
            if (undo.parent_block != expected_parent ||
                undo.previous_root != expected_root ||
                undo.block_id == snapshot.genesis_block)
                return std::nullopt;
            for (const auto& nullifier : undo.nullifiers)
                if (!expected_nullifiers.insert(nullifier).second)
                    return std::nullopt;
            for (const auto& commitment : undo.commitments)
                if (!expected_commitments.insert(commitment).second)
                    return std::nullopt;
            ++restored.active_roots_[undo.resulting_root];
            expected_parent = undo.block_id;
            expected_root = undo.resulting_root;
        }
        if (snapshot.tip_block != expected_parent ||
            snapshot.current_root != expected_root ||
            snapshot.nullifiers.size() != expected_nullifiers.size() ||
            snapshot.commitments.size() != expected_commitments.size() ||
            !std::equal(snapshot.nullifiers.begin(), snapshot.nullifiers.end(),
                        expected_nullifiers.begin()) ||
            !std::equal(snapshot.commitments.begin(), snapshot.commitments.end(),
                        expected_commitments.begin()))
            return std::nullopt;
        restored.tip_block_ = snapshot.tip_block;
        restored.current_root_ = snapshot.current_root;
        restored.nullifiers_ = std::move(expected_nullifiers);
        restored.commitments_ = std::move(expected_commitments);
        restored.history_ = snapshot.history;
        return restored;
    }

    PrivateAdmissionError connect(const Hash256& block_id,
                                  const Hash256& resulting_root,
                                  const PrivateBlockAdmission::Prepared& prepared) {
        if (prepared.base_tip_ != tip_block_ || prepared.base_root_ != current_root_)
            return PrivateAdmissionError::stale_preparation;
        if (block_id == genesis_block_)
            return PrivateAdmissionError::duplicate_block;
        for (const auto& undo : history_)
            if (undo.block_id == block_id)
                return PrivateAdmissionError::duplicate_block;
        for (const auto& nullifier : prepared.nullifiers_)
            if (spent(nullifier)) return PrivateAdmissionError::repeated_nullifier;
        for (const auto& commitment : prepared.commitments_)
            if (contains_commitment(commitment))
                return PrivateAdmissionError::repeated_commitment;

        ShieldedUndo undo{block_id, tip_block_, current_root_, resulting_root,
                          prepared.nullifiers_, prepared.commitments_};
        history_.reserve(history_.size() + 1U);
        std::size_t inserted_nullifiers = 0U;
        std::size_t inserted_commitments = 0U;
        bool root_incremented = false;
        try {
            for (const auto& nullifier : prepared.nullifiers_) {
                if (!nullifiers_.insert(nullifier).second) {
                    erase_prefix(nullifiers_, prepared.nullifiers_, inserted_nullifiers);
                    return PrivateAdmissionError::repeated_nullifier;
                }
                ++inserted_nullifiers;
            }
            for (const auto& commitment : prepared.commitments_) {
                if (!commitments_.insert(commitment).second) {
                    erase_prefix(commitments_, prepared.commitments_,
                                 inserted_commitments);
                    erase_prefix(nullifiers_, prepared.nullifiers_,
                                 inserted_nullifiers);
                    return PrivateAdmissionError::repeated_commitment;
                }
                ++inserted_commitments;
            }
            ++active_roots_[resulting_root];
            root_incremented = true;
            history_.push_back(std::move(undo));
        } catch (...) {
            if (root_incremented) {
                const auto root = active_roots_.find(resulting_root);
                if (root != active_roots_.end() && --root->second == 0U)
                    active_roots_.erase(root);
            }
            erase_prefix(commitments_, prepared.commitments_, inserted_commitments);
            erase_prefix(nullifiers_, prepared.nullifiers_, inserted_nullifiers);
            throw;
        }
        tip_block_ = block_id;
        current_root_ = resulting_root;
        return PrivateAdmissionError::none;
    }

    PrivateAdmissionError disconnect(const Hash256& block_id) {
        if (history_.empty()) return PrivateAdmissionError::disconnect_past_genesis;
        const auto& undo = history_.back();
        if (undo.block_id != block_id)
            return PrivateAdmissionError::disconnect_order_mismatch;
        for (const auto& nullifier : undo.nullifiers) nullifiers_.erase(nullifier);
        for (const auto& commitment : undo.commitments)
            commitments_.erase(commitment);
        const auto root = active_roots_.find(undo.resulting_root);
        if (root != active_roots_.end() && --root->second == 0U)
            active_roots_.erase(root);
        tip_block_ = undo.parent_block;
        current_root_ = undo.previous_root;
        history_.pop_back();
        return PrivateAdmissionError::none;
    }
};

inline PrivateBlockAdmission::Outcome PrivateBlockAdmission::prepare_verified(
        const ShieldedState& state, const Hash256& parent,
        const std::vector<TransactionEnvelope>& transactions,
        std::vector<VerifiedPrivateEffects> effects,
        const PrivateAdmissionLimits& limits) {
    if (parent != state.tip())
        return {PrivateAdmissionError::wrong_parent, PrivateProofError::none,
                std::nullopt};
    if (effects.size() != transactions.size())
        return {PrivateAdmissionError::proof_verification_failed,
                PrivateProofError::backend_unavailable, std::nullopt};

    std::set<Hash256> transaction_ids;
    std::set<Hash256> candidate_nullifiers;
    std::set<Hash256> candidate_commitments;
    std::vector<Hash256> ordered_transaction_ids;
    std::vector<Hash256> ordered_nullifiers;
    std::vector<Hash256> ordered_commitments;
    Amount total_fees = 0;

    ordered_transaction_ids.reserve(transactions.size());
    for (std::size_t index = 0U; index < transactions.size(); ++index) {
        const auto& transaction = transactions[index];
        if (transaction.body.size() > limits.max_transaction_body_bytes)
            return {PrivateAdmissionError::transaction_body_too_large,
                    PrivateProofError::none, std::nullopt};
        const auto id = transaction_id(transaction);
        if (!transaction_ids.insert(id).second)
            return {PrivateAdmissionError::duplicate_transaction,
                    PrivateProofError::none, std::nullopt};
        auto& verified = effects[index];
        if (verified.error != PrivateProofError::none)
            return {PrivateAdmissionError::proof_verification_failed,
                    verified.error, std::nullopt};
        if (verified.nullifiers.empty())
            return {PrivateAdmissionError::empty_spend,
                    PrivateProofError::none, std::nullopt};
        if (verified.commitments.empty())
            return {PrivateAdmissionError::empty_output,
                    PrivateProofError::none, std::nullopt};
        if (verified.nullifiers.size() > limits.max_nullifiers_per_transaction)
            return {PrivateAdmissionError::too_many_nullifiers,
                    PrivateProofError::none, std::nullopt};
        if (verified.commitments.size() > limits.max_commitments_per_transaction)
            return {PrivateAdmissionError::too_many_commitments,
                    PrivateProofError::none, std::nullopt};
        if (verified.nullifiers.size() >
                limits.max_private_actions_per_block ||
            verified.commitments.size() >
                limits.max_private_actions_per_block ||
            ordered_nullifiers.size() >
                limits.max_private_actions_per_block -
                    verified.nullifiers.size() ||
            ordered_commitments.size() >
                limits.max_private_actions_per_block -
                    verified.commitments.size())
            return {PrivateAdmissionError::too_many_private_actions,
                    PrivateProofError::none, std::nullopt};
        if (!state.has_anchor(verified.anchor))
            return {PrivateAdmissionError::unknown_anchor,
                    PrivateProofError::none, std::nullopt};
        for (const auto& nullifier : verified.nullifiers) {
            if (state.spent(nullifier) ||
                !candidate_nullifiers.insert(nullifier).second)
                return {PrivateAdmissionError::repeated_nullifier,
                        PrivateProofError::none, std::nullopt};
            ordered_nullifiers.push_back(nullifier);
        }
        for (const auto& commitment : verified.commitments) {
            if (state.contains_commitment(commitment) ||
                !candidate_commitments.insert(commitment).second)
                return {PrivateAdmissionError::repeated_commitment,
                        PrivateProofError::none, std::nullopt};
            ordered_commitments.push_back(commitment);
        }
        if (verified.fee < 0)
            return {PrivateAdmissionError::negative_fee,
                    PrivateProofError::none, std::nullopt};
        if (verified.fee > std::numeric_limits<Amount>::max() - total_fees)
            return {PrivateAdmissionError::fee_overflow,
                    PrivateProofError::none, std::nullopt};
        total_fees += verified.fee;
        ordered_transaction_ids.push_back(id);
    }

    Prepared prepared{state.tip(), state.root(),
                      std::move(ordered_transaction_ids),
                      std::move(ordered_nullifiers),
                      std::move(ordered_commitments), total_fees};
    return {PrivateAdmissionError::none, PrivateProofError::none,
            std::optional<Prepared>{std::move(prepared)}};
}

inline PrivateBlockAdmission::Outcome PrivateBlockAdmission::prepare(
        const ShieldedState& state, const Hash256& parent,
        const std::vector<TransactionEnvelope>& transactions,
        const PrivateTransactionVerifier& verifier,
        const PrivateAdmissionLimits& limits) {
    std::vector<VerifiedPrivateEffects> effects;
    effects.reserve(transactions.size());
    for (const auto& transaction : transactions)
        effects.push_back(verifier.verify(transaction));
    return prepare_verified(state, parent, transactions, std::move(effects),
                            limits);
}

inline PrivateBlockAdmission::Outcome PrivateBlockAdmission::prepare_parallel(
        const ShieldedState& state, const Hash256& parent,
        const std::vector<TransactionEnvelope>& transactions,
        const PrivateTransactionVerifier& verifier,
        const PrivateAdmissionLimits& limits, std::size_t workers) {
    if (workers <= 1U || transactions.size() <= 1U)
        return prepare(state, parent, transactions, verifier, limits);
    workers = std::min(workers, transactions.size());
    std::vector<VerifiedPrivateEffects> effects(transactions.size());
    std::atomic<std::size_t> next{0U};
    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (std::size_t worker = 0U; worker < workers; ++worker) {
        threads.emplace_back([&] {
            for (;;) {
                const auto index = next.fetch_add(1U, std::memory_order_relaxed);
                if (index >= transactions.size()) break;
                effects[index] = verifier.verify(transactions[index]);
            }
        });
    }
    for (auto& thread : threads) thread.join();
    return prepare_verified(state, parent, transactions, std::move(effects),
                            limits);
}

} // namespace onuros
