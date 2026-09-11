#pragma once

#include "onuros/private_transaction.hpp"

#include <cstddef>
#include <cstdint>

namespace onuros {

enum class OrchardFfiStatus : int {
    verified = 0,
    malformed = 1,
    invalid_proof = 2,
    invalid_signature = 3,
    invalid_balance = 4,
    internal_error = 5
};

using OrchardVerifyFunction = int (*)(const std::uint8_t*, std::size_t,
                                      const std::uint8_t*);

#ifdef ONUROS_ORCHARD_FFI_ENABLED
extern "C" int onuros_orchard_verify(const std::uint8_t*, std::size_t,
                                      const std::uint8_t*);
#endif

class OrchardFfiBackend final : public PrivateProofBackend {
    OrchardVerifyFunction verify_;

    static OrchardVerifyFunction default_verifier() {
#ifdef ONUROS_ORCHARD_FFI_ENABLED
        return &onuros_orchard_verify;
#else
        return nullptr;
#endif
    }

public:
    explicit OrchardFfiBackend(
            OrchardVerifyFunction verify = default_verifier())
        : verify_(verify) {}

    VerifiedPrivateEffects verify(
            const PrivateTransactionBundle& bundle,
            const Hash256& transaction_id_value) const override {
        if (verify_ == nullptr)
            return {PrivateProofError::backend_unavailable, {}, {}, {}, 0};
        const auto encoded = encode_private_bundle(bundle);
        const auto status = static_cast<OrchardFfiStatus>(verify_(
            encoded.data(), encoded.size(), transaction_id_value.data()));
        PrivateProofError error = PrivateProofError::invalid_proof;
        switch (status) {
        case OrchardFfiStatus::verified:
            error = PrivateProofError::none;
            break;
        case OrchardFfiStatus::malformed:
            error = PrivateProofError::malformed_encoding;
            break;
        case OrchardFfiStatus::invalid_proof:
            error = PrivateProofError::invalid_proof;
            break;
        case OrchardFfiStatus::invalid_signature:
            error = PrivateProofError::invalid_signature;
            break;
        case OrchardFfiStatus::invalid_balance:
            error = PrivateProofError::invalid_balance;
            break;
        case OrchardFfiStatus::internal_error:
            error = PrivateProofError::backend_unavailable;
            break;
        }
        if (error != PrivateProofError::none)
            return {error, {}, {}, {}, 0};

        std::vector<Hash256> nullifiers;
        std::vector<Hash256> commitments;
        nullifiers.reserve(bundle.actions.size());
        commitments.reserve(bundle.actions.size());
        for (const auto& action : bundle.actions) {
            nullifiers.push_back(action.nullifier);
            commitments.push_back(action.note_commitment);
        }
        return {PrivateProofError::none, bundle.anchor,
                std::move(nullifiers), std::move(commitments), bundle.fee};
    }
};

} // namespace onuros
