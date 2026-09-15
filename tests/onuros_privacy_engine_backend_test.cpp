#include "onuros/onuros_privacy_engine_backend.hpp"
#include "onuros/privacy_engine_network.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

onuros_privacy_status_v1 next_status = ONUROS_PRIVACY_OK;

onuros_privacy_status_v1 validate(
    const onuros_privacy_engine_v1*, const std::uint8_t* payment,
    std::size_t payment_length, const onuros_accepted_root_v1* roots,
    std::size_t root_count, std::uint32_t chain_height,
    std::uint32_t max_root_age, onuros_validated_payment_v1* result) {
    check(payment != nullptr && payment_length == 584U);
    check(roots != nullptr && root_count == 1U);
    check(chain_height == 120U && max_root_age == 100U);
    if (next_status != ONUROS_PRIVACY_OK) return next_status;
    result->root_height = roots[0].height;
    result->fee = 7U;
    result->nullifier[31] = 11U;
    result->output_commitments[0][31] = 12U;
    result->output_commitments[1][31] = 13U;
    return ONUROS_PRIVACY_OK;
}

} // namespace

int main() {
    onuros_accepted_root_v1 root{};
    root.height = 100U;
    root.root[31] = 9U;
    const auto* engine = reinterpret_cast<const onuros_privacy_engine_v1*>(1U);
    OnurosPrivacyEngineBackend backend(engine, &validate, {root}, 120U, 100U);

    TransactionEnvelope payment;
    payment.version = onuros_private_payment_envelope_version;
    payment.body.resize(onuros_private_payment_bytes);
    const auto accepted = backend.verify(payment);
    check(accepted.error == PrivateProofError::none);
    check(accepted.anchor[31] == 9U);
    check(accepted.nullifiers.size() == 1U && accepted.nullifiers[0][31] == 11U);
    check(accepted.commitments.size() == 2U);
    check(accepted.commitments[0][31] == 12U &&
          accepted.commitments[1][31] == 13U);
    check(accepted.fee == 7);

    Hash256 genesis{};
    genesis[0] = 1U;
    ShieldedState network_state(genesis, accepted.anchor);
    PrivacyEngineNetworkAdmission network(
        engine, &validate, {root}, 120U, 100U, network_state,
        {16U, 64U * 1024U, 64U, 1024U, 4U},
        16U, 64U * 1024U, {}, {64U * 1024U, 16U, 1024U});
    const P2pFrame payment_frame{
        stage7_protocol_version, P2pMessageType::transactions, 1U,
        encode_network_transactions({payment})};
    const auto network_result = network.handle_frame_parallel(payment_frame, 2U);
    check(network_result.has_value() && network_result->accepted());
    check(network.mempool().size() == 1U);
    check(network.mempool().total_actions() == 2U);
    const auto duplicate_network = network.handle_frame(payment_frame.request_id,
                                                        payment_frame);
    check(duplicate_network.has_value() && !duplicate_network->accepted());

    ShieldedState state(genesis, accepted.anchor);
    const PrivateAdmissionLimits limits{
        onuros_private_payment_bytes, 1U, 2U, 16U};
    auto prepared = PrivateBlockAdmission::prepare(
        state, genesis, {payment}, backend, limits);
    check(prepared.accepted());
    Hash256 block{};
    block[0] = 2U;
    Hash256 resulting_root{};
    resulting_root[0] = 3U;
    check(state.connect(block, resulting_root, *prepared.prepared) ==
          PrivateAdmissionError::none);
    check(state.tip() == block && state.root() == resulting_root);
    check(state.spent(accepted.nullifiers[0]));
    check(state.contains_commitment(accepted.commitments[0]));
    check(state.contains_commitment(accepted.commitments[1]));

    const auto duplicate = PrivateBlockAdmission::prepare(
        state, block, {payment}, backend, limits);
    check(duplicate.error == PrivateAdmissionError::repeated_nullifier);
    const auto snapshot = state.snapshot();
    const auto restored = ShieldedState::restore(snapshot);
    check(restored.has_value());
    check(restored->tip() == block && restored->root() == resulting_root);
    check(restored->spent(accepted.nullifiers[0]));
    check(state.disconnect(block) == PrivateAdmissionError::none);
    check(state.tip() == genesis && state.root() == accepted.anchor);
    check(!state.spent(accepted.nullifiers[0]));

    auto malformed = payment;
    malformed.body.pop_back();
    check(backend.verify(malformed).error ==
          PrivateProofError::malformed_encoding);
    malformed = payment;
    malformed.version = 2U;
    check(backend.verify(malformed).error ==
          PrivateProofError::malformed_encoding);

    next_status = ONUROS_PRIVACY_INVALID_PROOF;
    check(backend.verify(payment).error == PrivateProofError::invalid_proof);
    next_status = ONUROS_PRIVACY_ROOT_EXPIRED;
    check(backend.verify(payment).error ==
          PrivateProofError::invalid_effect_binding);
    next_status = ONUROS_PRIVACY_INTERNAL_PANIC;
    check(backend.verify(payment).error ==
          PrivateProofError::backend_unavailable);
    return 0;
}
