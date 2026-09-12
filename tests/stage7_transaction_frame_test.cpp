#include "onuros/stage7_node_admission.hpp"

#include <cstdlib>
#include <vector>

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

TransactionEnvelope transaction(std::uint8_t selector) {
    return {2U, {selector, selector}};
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
                {value(100U + candidate.body[0])},
                {value(200U + candidate.body[0])}, 1};
    }
};

PrivateMempoolLimits mempool_limits() {
    return {16U, 4096U, 16U, 64U, 2U};
}

P2pFrame frame(const std::vector<TransactionEnvelope>& transactions) {
    P2pFrame result;
    result.type = P2pMessageType::transactions;
    result.request_id = 7U;
    result.payload = encode_network_transactions(transactions);
    return result;
}
} // namespace

int main() {
    const auto encoded =
        encode_network_transactions({transaction(1U), transaction(2U)});
    const auto decoded =
        decode_network_transactions(encoded, NetworkTransactionBatchLimits{});
    check(decoded.accepted() && decoded.transactions.size() == 2U);
    check(transaction_id(decoded.transactions[0]) ==
          transaction_id(transaction(1U)));
    check(transaction_id(decoded.transactions[1]) ==
          transaction_id(transaction(2U)));

    auto malformed = encoded;
    malformed.push_back(0U);
    check(decode_network_transactions(
              malformed, NetworkTransactionBatchLimits{}).error ==
          NetworkTransactionBatchDecodeError::malformed_payload);
    check(decode_network_transactions(
              {}, NetworkTransactionBatchLimits{}).error ==
          NetworkTransactionBatchDecodeError::malformed_payload);
    check(decode_network_transactions(
              encode_network_transactions({}),
              NetworkTransactionBatchLimits{}).error ==
          NetworkTransactionBatchDecodeError::empty_batch);

    NetworkTransactionBatchLimits one_transaction;
    one_transaction.maximum_transactions = 1U;
    check(decode_network_transactions(encoded, one_transaction).error ==
          NetworkTransactionBatchDecodeError::too_many_transactions);
    NetworkTransactionBatchLimits three_bytes;
    three_bytes.maximum_payload_bytes = 3U;
    check(decode_network_transactions(encoded, three_bytes).error ==
          NetworkTransactionBatchDecodeError::oversized_payload);

    const auto genesis = value(1U);
    const auto root = value(2U);
    ShieldedState state(genesis, root);
    Verifier verifier(root);

    PrivateMempool accepted_mempool(mempool_limits());
    ValidatedRelayPool accepted_relay(16U, 4096U);
    Stage7NodeAdmission accepted_admission(
        state, verifier, accepted_mempool, accepted_relay);
    const auto accepted =
        accepted_admission.admit_frame(frame({transaction(1U),
                                              transaction(2U)}));
    check(accepted.accepted() && accepted.accepted_transactions == 2U);
    check(accepted_mempool.size() == 2U && accepted_relay.size() == 2U);
    check(accepted_admission.metrics().transaction_frames_accepted == 1U);

    P2pFrame ping;
    ping.type = P2pMessageType::ping;
    check(accepted_admission.admit_frame(ping).error ==
          NetworkFrameAdmissionError::unsupported_message);

    PrivateMempool invalid_mempool(mempool_limits());
    ValidatedRelayPool invalid_relay(16U, 4096U);
    Stage7NodeAdmission invalid_admission(
        state, verifier, invalid_mempool, invalid_relay);
    const auto invalid = invalid_admission.admit_frame(
        frame({transaction(3U), transaction(0U)}));
    check(invalid.error == NetworkFrameAdmissionError::transaction_rejected);
    check(invalid.failed_index && *invalid.failed_index == 1U);
    check(invalid.transaction_result.mempool_result.proof_error ==
          PrivateProofError::invalid_proof);
    check(invalid_mempool.size() == 0U && invalid_relay.size() == 0U);
    check(invalid_admission.metrics().transactions_accepted == 0U);
    check(invalid_admission.metrics().transaction_frames_rejected == 1U);

    PrivateMempool duplicate_mempool(mempool_limits());
    ValidatedRelayPool duplicate_relay(16U, 4096U);
    Stage7NodeAdmission duplicate_admission(
        state, verifier, duplicate_mempool, duplicate_relay);
    const auto duplicate = duplicate_admission.admit_frame(
        frame({transaction(4U), transaction(4U)}));
    check(duplicate.error ==
          NetworkFrameAdmissionError::transaction_rejected);
    check(duplicate_mempool.size() == 0U && duplicate_relay.size() == 0U);

    PrivateMempool bounded_mempool(mempool_limits());
    ValidatedRelayPool bounded_relay(1U, 4096U);
    Stage7NodeAdmission bounded_admission(
        state, verifier, bounded_mempool, bounded_relay);
    const auto bounded = bounded_admission.admit_frame(
        frame({transaction(5U), transaction(6U)}));
    check(bounded.error ==
          NetworkFrameAdmissionError::transaction_rejected);
    check(bounded.transaction_result.error ==
          NetworkTransactionAdmissionError::relay_rejected);
    check(bounded_mempool.size() == 0U && bounded_relay.size() == 0U);
    check(bounded_admission.metrics().transactions_accepted == 0U);
    return 0;
}
