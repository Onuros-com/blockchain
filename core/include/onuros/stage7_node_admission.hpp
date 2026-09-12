#pragma once

#include "onuros/private_mempool.hpp"
#include "onuros/stage7_relay.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace onuros {

enum class NetworkTransactionAdmissionError {
    none,
    mempool_rejected,
    relay_rejected
};

struct NetworkTransactionAdmissionResult {
    NetworkTransactionAdmissionError error =
        NetworkTransactionAdmissionError::none;
    PrivateMempoolResult mempool_result{};

    bool accepted() const noexcept {
        return error == NetworkTransactionAdmissionError::none;
    }
};

enum class NetworkBlockAdmissionError {
    none,
    unavailable,
    rejected
};

struct NetworkAdmissionMetrics {
    std::size_t transactions_accepted = 0U;
    std::size_t transactions_mempool_rejected = 0U;
    std::size_t transactions_relay_rejected = 0U;
    std::size_t blocks_accepted = 0U;
    std::size_t blocks_rejected = 0U;
};

// Fail-closed boundary between decoded peer messages and full-node state.
//
// The event loop may deliver candidates here, but it cannot authorize them.
// Transactions must pass the ordinary private mempool verifier before entering
// relay state. Blocks are handed to the supplied full-node callback, which is
// responsible for all contextual, proof-of-work, private-state and persistence
// checks.
class Stage7NodeAdmission {
public:
    using BlockAdmission =
        std::function<bool(const Block&, std::uint64_t now)>;

private:
    const ShieldedState& shielded_state_;
    const PrivateTransactionVerifier& verifier_;
    PrivateMempool& mempool_;
    ValidatedRelayPool& relay_pool_;
    BlockAdmission block_admission_;
    NetworkAdmissionMetrics metrics_{};

public:
    Stage7NodeAdmission(const ShieldedState& shielded_state,
                        const PrivateTransactionVerifier& verifier,
                        PrivateMempool& mempool,
                        ValidatedRelayPool& relay_pool,
                        BlockAdmission block_admission = {})
        : shielded_state_(shielded_state), verifier_(verifier),
          mempool_(mempool), relay_pool_(relay_pool),
          block_admission_(std::move(block_admission)) {}

    NetworkTransactionAdmissionResult admit_transaction(
            const TransactionEnvelope& transaction) {
        const auto admitted =
            mempool_.add(transaction, shielded_state_, verifier_);
        if (!admitted.accepted()) {
            ++metrics_.transactions_mempool_rejected;
            return {NetworkTransactionAdmissionError::mempool_rejected,
                    admitted};
        }
        if (!relay_pool_.remember_validated(transaction)) {
            // Admission is atomic across the mempool and the bounded relay
            // cache. A transaction that cannot be relayed is not left behind
            // as a partially admitted network candidate.
            mempool_.remove_confirmed({transaction});
            ++metrics_.transactions_relay_rejected;
            return {NetworkTransactionAdmissionError::relay_rejected, {}};
        }
        ++metrics_.transactions_accepted;
        return {};
    }

    NetworkBlockAdmissionError admit_block(const Block& block,
                                            std::uint64_t now) {
        if (!block_admission_) {
            ++metrics_.blocks_rejected;
            return NetworkBlockAdmissionError::unavailable;
        }
        if (!block_admission_(block, now)) {
            ++metrics_.blocks_rejected;
            return NetworkBlockAdmissionError::rejected;
        }
        ++metrics_.blocks_accepted;
        return NetworkBlockAdmissionError::none;
    }

    const NetworkAdmissionMetrics& metrics() const noexcept {
        return metrics_;
    }
};

} // namespace onuros
