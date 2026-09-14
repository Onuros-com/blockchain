#include "onuros/onuros_privacy_engine_backend.hpp"

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
