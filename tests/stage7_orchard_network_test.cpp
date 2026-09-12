#include "onuros/stage7_orchard_network.hpp"

#include <cstdlib>
#include <vector>

namespace {
using namespace onuros;

std::size_t verifier_calls = 0U;

void check(bool condition) {
    if (!condition) std::abort();
}

Hash256 value(std::uint8_t byte) {
    Hash256 result{};
    result.back() = byte;
    return result;
}

int accept_orchard_fixture(const std::uint8_t* encoded,
                           std::size_t encoded_size,
                           const std::uint8_t* digest) {
    ++verifier_calls;
    return encoded != nullptr && encoded_size != 0U && digest != nullptr
        ? static_cast<int>(OrchardFfiStatus::verified)
        : static_cast<int>(OrchardFfiStatus::internal_error);
}

TransactionEnvelope private_transaction(std::uint8_t selector,
                                        const Hash256& anchor) {
    PrivateTransactionBundle bundle;
    bundle.anchor = anchor;
    bundle.value_balance = 1;
    bundle.fee = 1;
    PrivateActionBundle action;
    action.value_commitment = value(static_cast<std::uint8_t>(20U + selector));
    action.nullifier = value(static_cast<std::uint8_t>(40U + selector));
    action.randomized_key = value(static_cast<std::uint8_t>(60U + selector));
    action.note_commitment = value(static_cast<std::uint8_t>(80U + selector));
    action.ephemeral_key = value(static_cast<std::uint8_t>(100U + selector));
    bundle.actions.push_back(action);
    bundle.proof.resize(
        orchard_proof_base_size + orchard_proof_per_action_size);
    bundle.proof.front() = selector;
    return make_private_transaction(bundle);
}

PrivateBundleLimits bundle_limits() {
    return {1U << 20U, 8U, 1U << 20U};
}

PrivateMempoolLimits mempool_limits() {
    return {16U, 8U << 20U, 128U, 1U << 20U, 8U};
}

P2pFrame transaction_frame(
        const std::vector<TransactionEnvelope>& transactions) {
    P2pFrame frame;
    frame.type = P2pMessageType::transactions;
    frame.request_id = 9U;
    frame.payload = encode_network_transactions(transactions);
    return frame;
}
} // namespace

int main() {
    const auto genesis = value(1U);
    const auto root = value(2U);
    ShieldedState state(genesis, root);

    OrchardNetworkAdmission admission(
        &accept_orchard_fixture, state, bundle_limits(), mempool_limits(),
        16U, 8U << 20U);

    EventPeerId observed_peer = 0U;
    std::size_t observed_transactions = 0U;
    auto handler = admission.make_frame_handler(
        [&observed_peer, &observed_transactions](
                EventPeerId peer,
                const NetworkFrameAdmissionResult& result) {
            check(result.accepted());
            observed_peer = peer;
            observed_transactions = result.accepted_transactions;
        });

    const auto first = private_transaction(1U, root);
    handler(42U, transaction_frame({first}));
    check(observed_peer == 42U && observed_transactions == 1U);
    check(admission.mempool().size() == 1U);
    check(admission.relay_pool().size() == 1U);
    check(admission.metrics().transactions_accepted == 1U);
    check(verifier_calls == 1U);

    P2pFrame ping;
    ping.type = P2pMessageType::ping;
    handler(99U, ping);
    check(observed_peer == 42U);
    check(admission.metrics().transaction_frames_rejected == 0U);

    OrchardNetworkAdmission atomic(
        &accept_orchard_fixture, state, bundle_limits(), mempool_limits(),
        16U, 8U << 20U);
    const auto second = private_transaction(2U, root);
    auto malformed_private = private_transaction(3U, root);
    malformed_private.version = 1U;
    const auto rejected =
        atomic.handle_frame(7U, transaction_frame({second, malformed_private}));
    check(rejected.has_value());
    check(rejected->error ==
          NetworkFrameAdmissionError::transaction_rejected);
    check(rejected->failed_index && *rejected->failed_index == 1U);
    check(rejected->transaction_result.mempool_result.proof_error ==
          PrivateProofError::malformed_encoding);
    check(atomic.mempool().size() == 0U);
    check(atomic.relay_pool().size() == 0U);
    check(atomic.metrics().transactions_accepted == 0U);
    check(atomic.metrics().transaction_frames_rejected == 1U);
    return 0;
}
