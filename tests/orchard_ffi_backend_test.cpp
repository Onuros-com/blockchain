#include "onuros/orchard_ffi_backend.hpp"

#include <cstdlib>

namespace {

using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

int status_to_return = 0;
std::size_t observed_size = 0U;
Hash256 observed_digest{};
std::size_t observed_root_count = 0U;

int scripted_verify(const std::uint8_t*, std::size_t size,
                    const std::uint8_t* signature_digest) {
    observed_size = size;
    for (std::size_t i = 0; i < observed_digest.size(); ++i)
        observed_digest[i] = signature_digest[i];
    return status_to_return;
}

int scripted_root(const std::uint8_t* commitments, std::size_t count,
                  std::uint8_t* output) {
    observed_root_count = count;
    if (status_to_return != 0) return status_to_return;
    for (std::size_t i = 0; i < 32U; ++i)
        output[i] = commitments == nullptr ? 0U : commitments[i];
    return 0;
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
    const auto digest = private_signature_digest(candidate);

    OrchardFfiBackend unavailable(nullptr);
    check(unavailable.verify(candidate, digest).error ==
          PrivateProofError::backend_unavailable);

    OrchardFfiBackend backend(scripted_verify);
    status_to_return = static_cast<int>(OrchardFfiStatus::verified);
    const auto verified = backend.verify(candidate, digest);
    check(verified.error == PrivateProofError::none);
    check(verified.anchor == candidate.anchor);
    check(verified.nullifiers.size() == 1U &&
          verified.nullifiers[0] == candidate.actions[0].nullifier);
    check(verified.commitments.size() == 1U &&
          verified.commitments[0] == candidate.actions[0].note_commitment);
    check(observed_size == envelope.body.size());
    check(observed_digest == digest);

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
        check(backend.verify(candidate, digest).error == expected[i]);
    }
    status_to_return = 99;
    check(backend.verify(candidate, digest).error == PrivateProofError::invalid_proof);

    OrchardFfiRootCalculator unavailable_root(nullptr);
    check(!unavailable_root.calculate({}, {}).has_value());
    OrchardFfiRootCalculator root(scripted_root);
    status_to_return = 0;
    const auto calculated = root.calculate({candidate.anchor},
                                           {candidate.actions[0].note_commitment});
    check(calculated.has_value() && *calculated == candidate.anchor);
    check(observed_root_count == 2U);
    const auto empty = root.calculate({}, {});
    check(empty.has_value() && *empty == Hash256{});
    status_to_return = static_cast<int>(OrchardFfiStatus::malformed);
    check(!root.calculate({candidate.anchor}, {}).has_value());
#ifdef ONUROS_ORCHARD_FFI_ENABLED
    OrchardFfiRootCalculator linked_root;
    check(linked_root.calculate({}, {}).has_value());
#endif
    return 0;
}
