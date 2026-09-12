#pragma once

#include "onuros/private_mempool.hpp"
#include "onuros/stage7_relay.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace onuros {

struct NetworkTransactionBatchLimits {
    std::size_t maximum_payload_bytes = 256U * 1024U;
    std::uint32_t maximum_transactions = 64U;
    std::uint32_t maximum_transaction_body_bytes = 64U * 1024U;
};

enum class NetworkTransactionBatchDecodeError {
    none,
    oversized_payload,
    malformed_payload,
    empty_batch,
    too_many_transactions
};

struct NetworkTransactionBatchDecodeResult {
    NetworkTransactionBatchDecodeError error =
        NetworkTransactionBatchDecodeError::none;
    std::vector<TransactionEnvelope> transactions;

    bool accepted() const noexcept {
        return error == NetworkTransactionBatchDecodeError::none;
    }
};

inline std::vector<std::uint8_t> encode_network_transactions(
        const std::vector<TransactionEnvelope>& transactions) {
    if (transactions.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("transaction batch exceeds canonical count");
    std::vector<std::uint8_t> output;
    detail::append_little_endian(
        output, static_cast<std::uint32_t>(transactions.size()));
    for (const auto& transaction : transactions) {
        const auto encoded = encode_transaction(transaction);
        output.insert(output.end(), encoded.begin(), encoded.end());
    }
    return output;
}

inline NetworkTransactionBatchDecodeResult decode_network_transactions(
        const std::vector<std::uint8_t>& payload,
        const NetworkTransactionBatchLimits& limits) {
    if (payload.size() > limits.maximum_payload_bytes)
        return {NetworkTransactionBatchDecodeError::oversized_payload, {}};
    detail::ByteReader reader(payload);
    std::uint32_t count = 0U;
    if (!reader.read_little_endian(count))
        return {NetworkTransactionBatchDecodeError::malformed_payload, {}};
    if (count == 0U)
        return {NetworkTransactionBatchDecodeError::empty_batch, {}};
    if (count > limits.maximum_transactions)
        return {NetworkTransactionBatchDecodeError::too_many_transactions, {}};
    if (count > reader.remaining() / 8U)
        return {NetworkTransactionBatchDecodeError::malformed_payload, {}};
    std::vector<TransactionEnvelope> transactions;
    transactions.reserve(count);
    for (std::uint32_t i = 0U; i < count; ++i) {
        TransactionEnvelope transaction;
        if (!detail::read_transaction(
                reader, limits.maximum_transaction_body_bytes, transaction))
            return {NetworkTransactionBatchDecodeError::malformed_payload, {}};
        transactions.push_back(std::move(transaction));
    }
    if (!reader.exhausted())
        return {NetworkTransactionBatchDecodeError::malformed_payload, {}};
    return {NetworkTransactionBatchDecodeError::none,
            std::move(transactions)};
}

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

enum class NetworkFrameAdmissionError {
    none,
    unsupported_message,
    invalid_payload,
    transaction_rejected
};

struct NetworkFrameAdmissionResult {
    NetworkFrameAdmissionError error = NetworkFrameAdmissionError::none;
    NetworkTransactionBatchDecodeError decode_error =
        NetworkTransactionBatchDecodeError::none;
    NetworkTransactionAdmissionResult transaction_result{};
    std::optional<std::size_t> failed_index;
    std::size_t accepted_transactions = 0U;

    bool accepted() const noexcept {
        return error == NetworkFrameAdmissionError::none;
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
    std::size_t transaction_frames_accepted = 0U;
    std::size_t transaction_frames_rejected = 0U;
    std::size_t blocks_accepted = 0U;
    std::size_t blocks_rejected = 0U;
};

// Fail-closed boundary between decoded peer messages and full-node state.
//
// PeerEventLoop callbacks may deliver frames here, but cannot authorize them.
// Transaction frames are bounded and decoded completely before admission. Every
// member must pass the ordinary private mempool verifier before entering relay
// state; a rejected member rolls back every earlier member in that frame.
// Blocks are handed to the supplied full-node callback, which remains
// responsible for contextual, proof-of-work, private-state and persistence
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
    NetworkTransactionBatchLimits batch_limits_;
    BlockAdmission block_admission_;
    NetworkAdmissionMetrics metrics_{};

    void rollback(const std::vector<Hash256>& identifiers) {
        for (auto identifier = identifiers.rbegin();
             identifier != identifiers.rend(); ++identifier) {
            (void)relay_pool_.forget_validated(*identifier);
            (void)mempool_.remove(*identifier);
        }
        metrics_.transactions_accepted -= identifiers.size();
    }

public:
    Stage7NodeAdmission(
            const ShieldedState& shielded_state,
            const PrivateTransactionVerifier& verifier,
            PrivateMempool& mempool,
            ValidatedRelayPool& relay_pool,
            BlockAdmission block_admission = {},
            NetworkTransactionBatchLimits batch_limits = {})
        : shielded_state_(shielded_state), verifier_(verifier),
          mempool_(mempool), relay_pool_(relay_pool),
          batch_limits_(batch_limits),
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
            (void)mempool_.remove(transaction_id(transaction));
            ++metrics_.transactions_relay_rejected;
            return {NetworkTransactionAdmissionError::relay_rejected, {}};
        }
        ++metrics_.transactions_accepted;
        return {};
    }

    NetworkFrameAdmissionResult admit_frame(const P2pFrame& frame) {
        if (frame.type != P2pMessageType::transactions) {
            ++metrics_.transaction_frames_rejected;
            NetworkFrameAdmissionResult result;
            result.error = NetworkFrameAdmissionError::unsupported_message;
            return result;
        }
        const auto decoded =
            decode_network_transactions(frame.payload, batch_limits_);
        if (!decoded.accepted()) {
            ++metrics_.transaction_frames_rejected;
            NetworkFrameAdmissionResult result;
            result.error = NetworkFrameAdmissionError::invalid_payload;
            result.decode_error = decoded.error;
            return result;
        }

        std::vector<Hash256> admitted;
        admitted.reserve(decoded.transactions.size());
        for (std::size_t i = 0U; i < decoded.transactions.size(); ++i) {
            const auto result = admit_transaction(decoded.transactions[i]);
            if (!result.accepted()) {
                rollback(admitted);
                ++metrics_.transaction_frames_rejected;
                NetworkFrameAdmissionResult rejected;
                rejected.error =
                    NetworkFrameAdmissionError::transaction_rejected;
                rejected.transaction_result = result;
                rejected.failed_index = i;
                return rejected;
            }
            admitted.push_back(transaction_id(decoded.transactions[i]));
        }
        ++metrics_.transaction_frames_accepted;
        NetworkFrameAdmissionResult accepted;
        accepted.accepted_transactions = admitted.size();
        return accepted;
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
