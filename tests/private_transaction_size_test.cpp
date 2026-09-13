#include "onuros/private_transaction_size.hpp"

#include <cstdlib>
#include <limits>

namespace {

using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

PrivateTransactionBundle bundle(std::size_t actions) {
    PrivateTransactionBundle result;
    result.actions.resize(actions);
    result.proof.resize(orchard_proof_base_size +
                        orchard_proof_per_action_size * actions);
    return result;
}

} // namespace

int main() {
    check(private_bundle_prefix_bytes == 65U);
    check(private_action_effect_bytes == 160U);
    check(private_action_ciphertext_bytes == 660U);
    check(private_action_authorization_bytes == 64U);
    check(private_action_encoded_bytes == 884U);

    const auto one = bundle(1U);
    const auto one_size = private_transaction_size(one);
    check(one_size.has_value());
    check(one_size->proof == 4'992U);
    check(one_size->body() == 6'009U);
    check(one_size->transaction() == 6'017U);
    check(one_size->body() == encode_private_bundle(one).size());
    check(one_size->transaction() ==
          encode_transaction(make_private_transaction(one)).size());

    const auto two = bundle(2U);
    const auto two_size = private_transaction_size(two);
    check(two_size.has_value());
    check(two_size->proof == 7'264U);
    check(two_size->body() == 9'165U);
    check(two_size->transaction() == 9'173U);
    check(two_size->effect_and_ciphertext_transaction() == 1'713U);
    check(two_size->body() == encode_private_bundle(two).size());
    check(two_size->transaction() ==
          encode_transaction(make_private_transaction(two)).size());

    const auto full = project_private_block_size(100U, 60U,
                                                  two_size->transaction());
    check(full.has_value());
    check(full->transaction_count == 6'000U);
    check(full->serialized_block_bytes == 55'038'164U);

    const auto current_privacy_floor = project_private_block_size(
        100U, 60U, two_size->effect_and_ciphertext_transaction());
    check(current_privacy_floor.has_value());
    check(current_privacy_floor->serialized_block_bytes == 10'278'164U);

    check(!private_transaction_size(
        std::numeric_limits<std::size_t>::max(), 1U));
    check(!private_transaction_size(
        1U, std::numeric_limits<std::size_t>::max()));
    check(!project_private_block_size(
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::size_t>::max()));
    return 0;
}
