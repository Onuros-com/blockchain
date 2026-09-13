#include "onuros/stage7_node_admission.hpp"

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

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

class ThreadRecordingVerifier final : public PrivateTransactionVerifier {
    Hash256 anchor_;
    mutable std::mutex mutex_;
    mutable std::set<std::thread::id> threads_;

public:
    explicit ThreadRecordingVerifier(Hash256 anchor) : anchor_(anchor) {}

    VerifiedPrivateEffects verify(
            const TransactionEnvelope& candidate) const override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            threads_.insert(std::this_thread::get_id());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return {PrivateProofError::none, anchor_,
                {value(300U + candidate.body[1])},
                {value(400U + candidate.body[0])}, 1};
    }

    std::size_t thread_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return threads_.size();
    }
};

class ThrowingVerifier final : public PrivateTransactionVerifier {
    Hash256 anchor_;

public:
    explicit ThrowingVerifier(Hash256 anchor) : anchor_(anchor) {}

    VerifiedPrivateEffects verify(
            const TransactionEnvelope& candidate) const override {
        if (candidate.body[0] == 0U)
            throw std::runtime_error("injected verifier failure");
        return {PrivateProofError::none, anchor_,
                {value(500U + candidate.body[1])},
                {value(600U + candidate.body[0])}, 1};
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

    ThreadRecordingVerifier parallel_verifier(root);
    PrivateMempool parallel_mempool({16U, 4096U, 16U, 64U, 2U});
    ValidatedRelayPool parallel_relay(16U, 4096U);
    Stage7NodeAdmission parallel(
        state, parallel_verifier, parallel_mempool, parallel_relay);
    std::vector<TransactionEnvelope> batch;
    for (std::uint8_t selector = 10U; selector < 18U; ++selector)
        batch.push_back(transaction(selector));
    const P2pFrame batch_frame{
        stage7_protocol_version, P2pMessageType::transactions, 50U,
        encode_network_transactions(batch)};
    const auto parallel_result = parallel.admit_frame_parallel(batch_frame, 4U);
    check(parallel_result.accepted_transactions == batch.size());
    check(parallel_mempool.size() == batch.size() &&
          parallel_relay.size() == batch.size());
    check(parallel_verifier.thread_count() > 1U);

    std::vector<TransactionEnvelope> second_batch;
    for (std::uint8_t selector = 18U; selector < 26U; ++selector)
        second_batch.push_back(transaction(selector));
    const P2pFrame second_frame{
        stage7_protocol_version, P2pMessageType::transactions, 51U,
        encode_network_transactions(second_batch)};
    const auto second_result =
        parallel.admit_frame_parallel(second_frame, 4U);
    check(second_result.accepted_transactions == second_batch.size());
    check(parallel_mempool.size() == 16U && parallel_relay.size() == 16U);
    check(parallel_verifier.thread_count() == 4U);
    const auto& parallel_metrics = parallel.metrics();
    check(parallel_metrics.verification_batches == 2U);
    check(parallel_metrics.verification_tasks == 16U);
    check(parallel_metrics.verification_workers == 4U);
    check(parallel_metrics.verification_pool_starts == 1U);
    check(parallel_metrics.verification_queue_limit == 64U);
    check(parallel_metrics.verification_queue_high_watermark == 8U);
    check(parallel_metrics.verification_microseconds > 0U);

    PrivateMempool failure_mempool({8U, 4096U, 8U, 64U, 2U});
    ValidatedRelayPool failure_relay(8U, 4096U);
    Stage7NodeAdmission failure(
        state, verifier, failure_mempool, failure_relay);
    const std::vector<TransactionEnvelope> failure_batch{
        transaction(30U), transaction(0U), transaction(31U)};
    const P2pFrame failure_frame{
        stage7_protocol_version, P2pMessageType::transactions, 52U,
        encode_network_transactions(failure_batch)};
    const auto failure_result =
        failure.admit_frame_parallel(failure_frame, 1U);
    check(failure_result.error ==
          NetworkFrameAdmissionError::transaction_rejected);
    check(failure_result.failed_index && *failure_result.failed_index == 1U);
    check(failure_mempool.size() == 0U && failure_relay.size() == 0U);
    check(failure.metrics().transactions_accepted == 0U);
    check(failure.metrics().verification_workers == 1U);
    check(failure.metrics().verification_pool_starts == 1U);

    ThrowingVerifier throwing_verifier(root);
    PrivateMempool throwing_mempool({8U, 4096U, 8U, 64U, 2U});
    ValidatedRelayPool throwing_relay(8U, 4096U);
    Stage7NodeAdmission throwing(
        state, throwing_verifier, throwing_mempool, throwing_relay);
    bool exception_propagated = false;
    try {
        (void)throwing.admit_frame_parallel(failure_frame, 2U);
    } catch (const std::runtime_error&) {
        exception_propagated = true;
    }
    check(exception_propagated);
    check(throwing_mempool.size() == 0U && throwing_relay.size() == 0U);
    return 0;
}
