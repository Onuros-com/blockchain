#pragma once

#include "onuros/private_admission.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace onuros {

struct PrivateMempoolLimits {
    std::size_t max_transactions;
    std::size_t max_total_bytes;
    std::size_t max_total_actions;
    std::size_t max_transaction_bytes;
    std::size_t max_actions_per_transaction;
};

enum class PrivateMempoolError {
    none,
    duplicate_transaction,
    transaction_too_large,
    verification_failed,
    empty_effects,
    too_many_actions,
    unknown_anchor,
    spent_nullifier,
    conflicting_nullifier,
    existing_commitment,
    conflicting_commitment,
    invalid_fee,
    pool_full
};

struct PrivateMempoolResult {
    PrivateMempoolError error = PrivateMempoolError::none;
    PrivateProofError proof_error = PrivateProofError::none;
    bool accepted() const { return error == PrivateMempoolError::none; }
};

class PrivateMempool {
    struct Entry {
        TransactionEnvelope transaction;
        VerifiedPrivateEffects effects;
        std::size_t encoded_bytes;
        std::uint64_t sequence;
    };

    PrivateMempoolLimits limits_;
    std::map<Hash256, Entry> entries_;
    std::set<Hash256> nullifiers_;
    std::set<Hash256> commitments_;
    std::size_t total_bytes_ = 0U;
    std::size_t total_actions_ = 0U;
    std::uint64_t next_sequence_ = 0U;

    void erase(typename std::map<Hash256, Entry>::iterator entry) {
        for (const auto& nullifier : entry->second.effects.nullifiers)
            nullifiers_.erase(nullifier);
        for (const auto& commitment : entry->second.effects.commitments)
            commitments_.erase(commitment);
        total_bytes_ -= entry->second.encoded_bytes;
        total_actions_ -= entry->second.effects.nullifiers.size();
        entries_.erase(entry);
    }

public:
    explicit PrivateMempool(PrivateMempoolLimits limits) : limits_(limits) {}

    // Commits effects produced by the configured proof verifier. This split is
    // used by the network admission path to verify independent transactions in
    // parallel, then serialize all conflict and capacity checks here.
    PrivateMempoolResult add_verified(
            const TransactionEnvelope& transaction,
            VerifiedPrivateEffects effects,
            const ShieldedState& state) {
        const auto id = transaction_id(transaction);
        if (entries_.find(id) != entries_.end())
            return {PrivateMempoolError::duplicate_transaction};
        const auto encoded_bytes = 8U + transaction.body.size();
        if (transaction.body.size() > limits_.max_transaction_bytes)
            return {PrivateMempoolError::transaction_too_large};
        if (effects.error != PrivateProofError::none)
            return {PrivateMempoolError::verification_failed, effects.error};
        if (effects.nullifiers.empty() || effects.commitments.empty())
            return {PrivateMempoolError::empty_effects};
        // A private-payment protocol is not required to have one output per
        // spend. Onuros Shielded Payment v1 has one nullifier and two output
        // commitments. Bound both effect sets independently; equality was an
        // Orchard-specific assumption and rejected valid 584-byte payments.
        if (effects.nullifiers.size() > limits_.max_actions_per_transaction ||
            effects.commitments.size() > limits_.max_actions_per_transaction)
            return {PrivateMempoolError::too_many_actions};
        if (!state.has_anchor(effects.anchor))
            return {PrivateMempoolError::unknown_anchor};
        if (effects.fee < 0)
            return {PrivateMempoolError::invalid_fee};
        std::set<Hash256> candidate_nullifiers;
        std::set<Hash256> candidate_commitments;
        for (const auto& nullifier : effects.nullifiers) {
            if (state.spent(nullifier))
                return {PrivateMempoolError::spent_nullifier};
            if (nullifiers_.find(nullifier) != nullifiers_.end() ||
                !candidate_nullifiers.insert(nullifier).second)
                return {PrivateMempoolError::conflicting_nullifier};
        }
        for (const auto& commitment : effects.commitments) {
            if (state.contains_commitment(commitment))
                return {PrivateMempoolError::existing_commitment};
            if (commitments_.find(commitment) != commitments_.end() ||
                !candidate_commitments.insert(commitment).second)
                return {PrivateMempoolError::conflicting_commitment};
        }
        const auto actions = std::max(effects.nullifiers.size(),
                                      effects.commitments.size());
        if (entries_.size() >= limits_.max_transactions ||
            encoded_bytes > limits_.max_total_bytes -
                std::min(limits_.max_total_bytes, total_bytes_) ||
            actions > limits_.max_total_actions -
                std::min(limits_.max_total_actions, total_actions_))
            return {PrivateMempoolError::pool_full};
        if (next_sequence_ == std::numeric_limits<std::uint64_t>::max())
            return {PrivateMempoolError::pool_full};
        nullifiers_.insert(candidate_nullifiers.begin(),
                           candidate_nullifiers.end());
        commitments_.insert(candidate_commitments.begin(),
                            candidate_commitments.end());
        total_bytes_ += encoded_bytes;
        total_actions_ += actions;
        entries_.emplace(id, Entry{transaction, std::move(effects),
                                   encoded_bytes, next_sequence_++});
        return {};
    }

    PrivateMempoolResult add(const TransactionEnvelope& transaction,
                             const ShieldedState& state,
                             const PrivateTransactionVerifier& verifier) {
        return add_verified(transaction, verifier.verify(transaction), state);
    }

    std::vector<TransactionEnvelope> select(
            std::size_t max_transactions, std::size_t max_bytes,
            std::size_t max_actions) const {
        std::vector<const Entry*> ordered;
        ordered.reserve(entries_.size());
        for (const auto& entry : entries_) ordered.push_back(&entry.second);
        std::sort(ordered.begin(), ordered.end(),
                  [](const Entry* left, const Entry* right) {
            if (left->effects.fee != right->effects.fee)
                return left->effects.fee > right->effects.fee;
            return left->sequence < right->sequence;
        });
        std::vector<TransactionEnvelope> selected;
        std::size_t bytes = 0U;
        std::size_t actions = 0U;
        for (const auto* entry : ordered) {
            const auto entry_actions = entry->effects.nullifiers.size();
            if (selected.size() >= max_transactions) break;
            if (entry->encoded_bytes > max_bytes - std::min(max_bytes, bytes) ||
                entry_actions > max_actions - std::min(max_actions, actions))
                continue;
            selected.push_back(entry->transaction);
            bytes += entry->encoded_bytes;
            actions += entry_actions;
        }
        return selected;
    }

    bool remove(const Hash256& identifier) {
        const auto entry = entries_.find(identifier);
        if (entry == entries_.end()) return false;
        erase(entry);
        return true;
    }

    void remove_confirmed(
            const std::vector<TransactionEnvelope>& transactions) {
        for (const auto& transaction : transactions)
            (void)remove(transaction_id(transaction));
    }

    std::size_t revalidate(const ShieldedState& state,
                           const PrivateTransactionVerifier& verifier) {
        std::vector<TransactionEnvelope> transactions;
        transactions.reserve(entries_.size());
        for (const auto& entry : entries_)
            transactions.push_back(entry.second.transaction);
        const auto previous = entries_.size();
        entries_.clear();
        nullifiers_.clear();
        commitments_.clear();
        total_bytes_ = 0U;
        total_actions_ = 0U;
        next_sequence_ = 0U;
        for (const auto& transaction : transactions)
            (void)add(transaction, state, verifier);
        return previous - entries_.size();
    }

    std::size_t size() const { return entries_.size(); }
    std::size_t total_bytes() const { return total_bytes_; }
    std::size_t total_actions() const { return total_actions_; }
};

} // namespace onuros
