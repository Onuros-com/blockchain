#include "onuros/orchard_ffi_backend.hpp"

#include <cstdlib>

namespace {

using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

int status_to_return = 0;
std::size_t observed_size = 0U;
Hash256 observed_id{};

int scripted_verify(const std::uint8_t*, std::size_t size,
                    const std::uint8_t* transaction_id_value) {
    observed_size = size;
    for (std::size_t i = 0; i < observed_id.size(); ++i)
        observed_id[i] = transaction_id_value[i];
    return status_to_return;
}

PrivateTransactionBundle bundle() {
    PrivateTransactionBundle result;
    result.anchor.back() = 1U;
    PrivateActionBundle action;
    action.nullifier.back() = 2U;
    action.note_commitment.back() = 3U;
    result.actions.push_back(action);
    result.proof.resize(orchard_proof_base_size +
                        orchard_proof_per_action_size);
    return result;
}

} // namespace

int main() {
    const auto candidate = bundle();
    const auto envelope = make_private_transaction(candidate);
    const auto id = transaction_id(envelope);

    OrchardFfiBackend unavailable;
    check(unavailable.verify(candidate, id).error ==
          PrivateProofError::backend_unavailable);

    OrchardFfiBackend backend(scripted_verify);
    status_to_return = static_cast<int>(OrchardFfiStatus::verified);
    const auto verified = backend.verify(candidate, id);
    check(verified.error == PrivateProofError::none);
    check(verified.anchor == candidate.anchor);
    check(verified.nullifiers.size() == 1U &&
          verified.nullifiers[0] == candidate.actions[0].nullifier);
    check(verified.commitments.size() == 1U &&
          verified.commitments[0] == candidate.actions[0].note_commitment);
    check(observed_size == envelope.body.size());
    check(observed_id == id);

    const OrchardFfiStatus statuses[] = {
        OrchardFfiStatus::malformed, OrchardFfiStatus::invalid_proof,
        OrchardFfiStatus::invalid_signature, OrchardFfiStatus::invalid_balance,
        OrchardFfiStatus::internal_error};
    const PrivateProofError expected[] = {
        PrivateProofError::malformed_encoding, PrivateProofError::invalid_proof,
        PrivateProofError::invalid_signature, PrivateProofError::invalid_balance,
        PrivateProofError::backend_unavailable};
    for (std::size_t i = 0; i < 5U; ++i) {
        status_to_return = static_cast<int>(statuses[i]);
        check(backend.verify(candidate, id).error == expected[i]);
    }
    status_to_return = 99;
    check(backend.verify(candidate, id).error == PrivateProofError::invalid_proof);
    return 0;
}
