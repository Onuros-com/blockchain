#include "onuros/private_transaction.hpp"

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
    return 0;
}
