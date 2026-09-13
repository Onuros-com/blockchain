#pragma once

#include "onuros/private_mempool.hpp"
#include "onuros/stage7_relay.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
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
    std::size_t verification_batches = 0U;
    std::size_t verification_tasks = 0U;
    std::size_t verification_workers = 0U;
    std::size_t verification_pool_starts = 0U;
    std::size_t verification_queue_limit = 0U;
    std::size_t verification_queue_high_watermark = 0U;
    std::uint64_t verification_microseconds = 0U;
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
    class VerificationWorkerPool {
        struct Task {
            const TransactionEnvelope* transaction = nullptr;
            VerifiedPrivateEffects* effects = nullptr;
        };

        const PrivateTransactionVerifier& verifier_;
        const std::size_t queue_limit_;
        std::vector<std::thread> threads_;
        std::deque<Task> queue_;
        mutable std::mutex mutex_;
        std::mutex submission_mutex_;
        std::condition_variable work_available_;
        std::condition_variable batch_complete_;
        std::size_t outstanding_ = 0U;
        std::size_t queue_high_watermark_ = 0U;
        std::exception_ptr failure_;
        bool stopping_ = false;

        void stop() noexcept {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopping_ = true;
            }
            work_available_.notify_all();
            for (auto& thread : threads_)
                if (thread.joinable()) thread.join();
        }

        void work() noexcept {
            for (;;) {
                Task task;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    work_available_.wait(lock, [this] {
                        return stopping_ || !queue_.empty();
                    });
                    if (stopping_ && queue_.empty()) return;
                    task = queue_.front();
                    queue_.pop_front();
                }
                try {
                    *task.effects = verifier_.verify(*task.transaction);
                } catch (...) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (!failure_) failure_ = std::current_exception();
                }
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    --outstanding_;
                    if (outstanding_ == 0U) batch_complete_.notify_one();
                }
            }
        }

    public:
        VerificationWorkerPool(const PrivateTransactionVerifier& verifier,
                               std::size_t workers,
                               std::size_t queue_limit)
            : verifier_(verifier), queue_limit_(queue_limit) {
            if (workers == 0U || queue_limit == 0U)
                throw std::invalid_argument("invalid verification pool limits");
            threads_.reserve(workers);
            try {
                for (std::size_t worker = 0U; worker < workers; ++worker)
                    threads_.emplace_back([this] { work(); });
            } catch (...) {
                stop();
                throw;
            }
        }

        VerificationWorkerPool(const VerificationWorkerPool&) = delete;
        VerificationWorkerPool& operator=(const VerificationWorkerPool&) =
            delete;

        ~VerificationWorkerPool() { stop(); }

        std::vector<VerifiedPrivateEffects> verify(
                const std::vector<TransactionEnvelope>& transactions) {
            if (transactions.empty()) return {};
            if (transactions.size() > queue_limit_)
                throw std::length_error("verification queue limit exceeded");
            std::lock_guard<std::mutex> submission(submission_mutex_);
            std::vector<VerifiedPrivateEffects> effects(transactions.size());
            {
                std::lock_guard<std::mutex> lock(mutex_);
                failure_ = nullptr;
                outstanding_ = transactions.size();
                for (std::size_t index = 0U;
                     index < transactions.size(); ++index)
                    queue_.push_back({&transactions[index], &effects[index]});
                queue_high_watermark_ =
                    std::max(queue_high_watermark_, queue_.size());
            }
            work_available_.notify_all();
            std::exception_ptr failure;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                batch_complete_.wait(lock, [this] {
                    return outstanding_ == 0U;
                });
                failure = failure_;
            }
            if (failure) std::rethrow_exception(failure);
            return effects;
        }

        std::size_t workers() const noexcept { return threads_.size(); }
        std::size_t queue_limit() const noexcept { return queue_limit_; }

        std::size_t queue_high_watermark() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_high_watermark_;
        }
    };

    const ShieldedState& shielded_state_;
    const PrivateTransactionVerifier& verifier_;
    PrivateMempool& mempool_;
    ValidatedRelayPool& relay_pool_;
    NetworkTransactionBatchLimits batch_limits_;
    BlockAdmission block_admission_;
    NetworkAdmissionMetrics metrics_{};
    std::unique_ptr<VerificationWorkerPool> verification_pool_;
    std::size_t verification_pool_workers_ = 0U;

    void rollback(const std::vector<Hash256>& identifiers) {
        for (auto identifier = identifiers.rbegin();
             identifier != identifiers.rend(); ++identifier) {
            (void)relay_pool_.forget_validated(*identifier);
            (void)mempool_.remove(*identifier);
        }
        metrics_.transactions_accepted -= identifiers.size();
    }

    NetworkTransactionAdmissionResult commit_verified_transaction(
            const TransactionEnvelope& transaction,
            VerifiedPrivateEffects effects) {
        const auto admitted = mempool_.add_verified(
            transaction, std::move(effects), shielded_state_);
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

    NetworkFrameAdmissionResult commit_verified_frame(
            const std::vector<TransactionEnvelope>& transactions,
            std::vector<VerifiedPrivateEffects> effects) {
        std::vector<Hash256> admitted;
        admitted.reserve(transactions.size());
        for (std::size_t i = 0U; i < transactions.size(); ++i) {
            const auto result = commit_verified_transaction(
                transactions[i], std::move(effects[i]));
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
            admitted.push_back(transaction_id(transactions[i]));
        }
        ++metrics_.transaction_frames_accepted;
        NetworkFrameAdmissionResult accepted;
        accepted.accepted_transactions = admitted.size();
        return accepted;
    }

    std::optional<NetworkFrameAdmissionResult> decode_transaction_frame(
            const P2pFrame& frame,
            NetworkTransactionBatchDecodeResult& decoded) {
        if (frame.type != P2pMessageType::transactions) {
            ++metrics_.transaction_frames_rejected;
            NetworkFrameAdmissionResult result;
            result.error = NetworkFrameAdmissionError::unsupported_message;
            return result;
        }
        decoded = decode_network_transactions(frame.payload, batch_limits_);
        if (!decoded.accepted()) {
            ++metrics_.transaction_frames_rejected;
            NetworkFrameAdmissionResult result;
            result.error = NetworkFrameAdmissionError::invalid_payload;
            result.decode_error = decoded.error;
            return result;
        }
        return std::nullopt;
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
        return commit_verified_transaction(
            transaction, verifier_.verify(transaction));
    }

    NetworkFrameAdmissionResult admit_frame(const P2pFrame& frame) {
        NetworkTransactionBatchDecodeResult decoded;
        if (const auto rejected = decode_transaction_frame(frame, decoded))
            return *rejected;
        std::vector<VerifiedPrivateEffects> effects;
        effects.reserve(decoded.transactions.size());
        for (const auto& transaction : decoded.transactions)
            effects.push_back(verifier_.verify(transaction));
        return commit_verified_frame(decoded.transactions, std::move(effects));
    }

    // Proof verification dominates relay admission and is independent for each
    // member of a decoded frame. Workers only produce immutable effects;
    // mempool conflict checks and relay insertion remain ordered and atomic.
    NetworkFrameAdmissionResult admit_frame_parallel(
            const P2pFrame& frame, std::size_t requested_workers) {
        NetworkTransactionBatchDecodeResult decoded;
        if (const auto rejected = decode_transaction_frame(frame, decoded))
            return *rejected;
        const auto workers = std::max<std::size_t>(1U, std::min(
            requested_workers,
            static_cast<std::size_t>(batch_limits_.maximum_transactions)));
        if (!verification_pool_ || verification_pool_workers_ != workers) {
            verification_pool_ = std::make_unique<VerificationWorkerPool>(
                verifier_, workers, batch_limits_.maximum_transactions);
            verification_pool_workers_ = workers;
            ++metrics_.verification_pool_starts;
        }
        const auto started = std::chrono::steady_clock::now();
        auto effects = verification_pool_->verify(decoded.transactions);
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started);
        ++metrics_.verification_batches;
        metrics_.verification_tasks += decoded.transactions.size();
        metrics_.verification_workers = verification_pool_->workers();
        metrics_.verification_queue_limit = verification_pool_->queue_limit();
        metrics_.verification_queue_high_watermark = std::max(
            metrics_.verification_queue_high_watermark,
            verification_pool_->queue_high_watermark());
        metrics_.verification_microseconds +=
            static_cast<std::uint64_t>(elapsed.count());
        return commit_verified_frame(decoded.transactions, std::move(effects));
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
