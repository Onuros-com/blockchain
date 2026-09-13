#include "onuros/private_transaction.hpp"
#include "onuros/compact_private_effect.hpp"

#include <cstddef>
#include <iostream>
#include <vector>

namespace {

void report(std::size_t actions) {
    const auto layout = onuros::private_transaction_byte_layout(actions);
    std::cout << "layout=private_transaction actions=" << actions << '\n'
              << "bundle_magic_bytes=" << layout.bundle_magic << '\n'
              << "format_version_bytes=" << layout.format_version << '\n'
              << "proof_system_version_bytes="
              << layout.proof_system_version << '\n'
              << "flags_bytes=" << layout.flags << '\n'
              << "anchor_bytes=" << layout.anchor << '\n'
              << "value_balance_bytes=" << layout.value_balance << '\n'
              << "fee_bytes=" << layout.fee << '\n'
              << "action_count_bytes=" << layout.action_count << '\n'
              << "fixed_header_bytes=" << layout.fixed_header << '\n'
              << "action_core_bytes=" << layout.action_core << '\n'
              << "encrypted_note_bytes=" << layout.encrypted_notes << '\n'
              << "outgoing_ciphertext_bytes="
              << layout.outgoing_ciphertexts << '\n'
              << "spend_authorization_bytes="
              << layout.spend_authorizations << '\n'
              << "proof_length_bytes=" << layout.proof_length << '\n'
              << "proof_bytes=" << layout.proof << '\n'
              << "binding_signature_bytes="
              << layout.binding_signature << '\n'
              << "body_bytes=" << layout.body << '\n'
              << "transaction_envelope_bytes="
              << layout.transaction_envelope << '\n'
              << "single_transaction_batch_bytes="
              << layout.single_transaction_batch << '\n';
}

} // namespace

int main() {
    report(1U);
    report(2U);
    onuros::PrivateTransactionBundle vector;
    vector.actions.resize(2U);
    vector.proof.resize(onuros::orchard_proof_base_size +
                        2U * onuros::orchard_proof_per_action_size);
    std::cout << "commitment_scheme_version="
              << onuros::private_commitment_scheme_version << '\n'
              << "zero_vector_effect_digest="
              << onuros::hash_hex(onuros::private_effect_digest(vector)) << '\n'
              << "zero_vector_authorization_commitment="
              << onuros::hash_hex(
                     onuros::private_authorizing_data_commitment(vector))
              << '\n'
              << "zero_vector_pair_commitment="
              << onuros::hash_hex(
                     onuros::private_effect_authorization_commitment(vector))
              << '\n';
    constexpr std::size_t transfers = 6'000U;
    const auto compact_effect = onuros::compact_private_effect_encoded_size(2U);
    const auto compact_framed =
        compact_effect + onuros::compact_private_effect_length_size;
    onuros::CompactPrivateEffects compact_vector;
    compact_vector.actions.resize(2U);
    std::cout << "compact_effect_version="
              << onuros::compact_private_effect_version << '\n'
              << "compact_note_ciphertext_bytes="
              << onuros::compact_note_ciphertext_size << '\n'
              << "compact_two_action_effect_bytes=" << compact_effect << '\n'
              << "compact_two_action_framed_bytes=" << compact_framed << '\n'
              << "compact_6000_effect_bytes="
              << compact_framed * transfers << '\n'
              << "zero_vector_compact_effect_digest="
              << onuros::hash_hex(
                     onuros::compact_private_effect_digest(compact_vector))
              << '\n';
    return 0;
}
