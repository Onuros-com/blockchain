#include "onuros/persistent_shielded_state.hpp"
#include "onuros/private_transaction.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

class Generator {
    std::uint64_t state_ = 0x4f6e75726f734631ULL;
public:
    std::uint64_t next() {
        state_ ^= state_ << 13U;
        state_ ^= state_ >> 7U;
        state_ ^= state_ << 17U;
        return state_;
    }
    std::uint8_t byte() { return static_cast<std::uint8_t>(next()); }
};

PrivateTransactionBundle canonical_bundle() {
    PrivateTransactionBundle bundle;
    bundle.anchor.back() = 1U;
    bundle.value_balance = 7;
    bundle.fee = 7;
    PrivateActionBundle action;
    action.value_commitment.back() = 2U;
    action.nullifier.back() = 3U;
    action.randomized_key.back() = 4U;
    action.note_commitment.back() = 5U;
    action.ephemeral_key.back() = 6U;
    bundle.actions = {action};
    bundle.proof.resize(orchard_proof_base_size +
                        orchard_proof_per_action_size, 0x5aU);
    return bundle;
}

void exercise(const std::vector<std::uint8_t>& bytes,
              const PrivateBundleLimits& limits) {
    const TransactionEnvelope transaction{
        private_transaction_envelope_version, bytes};
    const auto decoded = decode_private_transaction(transaction, limits);
    if (decoded.accepted()) {
        check(encode_private_bundle(*decoded.bundle) == bytes);
        (void)private_signature_digest(*decoded.bundle);
    }
    (void)shielded_store_detail::decode(bytes, 4096U);
}

} // namespace

int main() {
    const PrivateBundleLimits limits{20'000U, 8U, 20'000U};
    Generator generator;

    const auto canonical = encode_private_bundle(canonical_bundle());
    exercise(canonical, limits);
    for (std::size_t offset = 0; offset < canonical.size(); ++offset) {
        auto mutated = canonical;
        mutated[offset] ^= static_cast<std::uint8_t>(1U << (offset % 8U));
        exercise(mutated, limits);
    }

    for (std::size_t case_number = 0; case_number < 2'048U; ++case_number) {
        const auto size = static_cast<std::size_t>(generator.next() % 32'769U);
        std::vector<std::uint8_t> bytes(size);
        for (auto& byte : bytes) byte = generator.byte();
        exercise(bytes, limits);
    }
    return 0;
}
