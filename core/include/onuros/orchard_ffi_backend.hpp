#pragma once

#include "onuros/private_block.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

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
using OrchardRootFunction = int (*)(const std::uint8_t*, std::size_t,
                                    std::uint8_t*);

#ifdef ONUROS_ORCHARD_FFI_ENABLED
extern "C" int onuros_orchard_verify(const std::uint8_t*, std::size_t,
                                      const std::uint8_t*);
extern "C" int onuros_orchard_root(const std::uint8_t*, std::size_t,
                                    std::uint8_t*);
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
            const Hash256& signature_digest) const override {
        if (verify_ == nullptr)
            return {PrivateProofError::backend_unavailable, {}, {}, {}, 0};
        const auto encoded = encode_private_bundle(bundle);
        const auto status = static_cast<OrchardFfiStatus>(verify_(
            encoded.data(), encoded.size(), signature_digest.data()));
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

class OrchardFfiRootCalculator final : public ShieldedRootCalculator {
    OrchardRootFunction root_;

    static OrchardRootFunction default_calculator() {
#ifdef ONUROS_ORCHARD_FFI_ENABLED
        return &onuros_orchard_root;
#else
        return nullptr;
#endif
    }

public:
    explicit OrchardFfiRootCalculator(
            OrchardRootFunction root = default_calculator()) : root_(root) {}

    std::optional<Hash256> calculate(
            const std::vector<Hash256>& active_commitments,
            const std::vector<Hash256>& new_commitments) const override {
        if (root_ == nullptr ||
            active_commitments.size() >
                std::numeric_limits<std::size_t>::max() -
                    new_commitments.size())
            return std::nullopt;
        std::vector<std::uint8_t> encoded;
        const auto count = active_commitments.size() + new_commitments.size();
        if (count > std::numeric_limits<std::size_t>::max() / Hash256{}.size())
            return std::nullopt;
        encoded.reserve(count * Hash256{}.size());
        for (const auto& commitment : active_commitments)
            encoded.insert(encoded.end(), commitment.begin(), commitment.end());
        for (const auto& commitment : new_commitments)
            encoded.insert(encoded.end(), commitment.begin(), commitment.end());
        Hash256 result{};
        const auto status = root_(encoded.empty() ? nullptr : encoded.data(),
                                  count, result.data());
        if (status != static_cast<int>(OrchardFfiStatus::verified))
            return std::nullopt;
        return result;
    }
};

} // namespace onuros
