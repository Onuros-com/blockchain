#pragma once

#include "onuros/onuros_privacy_engine_v1.h"
#include "onuros/private_admission.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace onuros {

inline constexpr std::uint32_t onuros_private_payment_envelope_version = 3U;
inline constexpr std::size_t onuros_private_payment_bytes = 584U;

class OnurosPrivacyEngineBackend final : public PrivateTransactionVerifier {
public:
    using ValidatePayment = onuros_privacy_status_v1 (*)(
        const onuros_privacy_engine_v1*, const std::uint8_t*, std::size_t,
        const onuros_accepted_root_v1*, std::size_t, std::uint32_t,
        std::uint32_t, onuros_validated_payment_v1*);

private:
    const onuros_privacy_engine_v1* engine_;
    ValidatePayment validate_;
    std::vector<onuros_accepted_root_v1> roots_;
    std::uint32_t chain_height_;
    std::uint32_t max_root_age_;

    static PrivateProofError map_error(onuros_privacy_status_v1 status) {
        switch (status) {
        case ONUROS_PRIVACY_INVALID_PAYMENT_ENCODING:
            return PrivateProofError::malformed_encoding;
        case ONUROS_PRIVACY_INVALID_PROOF:
            return PrivateProofError::invalid_proof;
        case ONUROS_PRIVACY_EFFECT_DIGEST_MISMATCH:
        case ONUROS_PRIVACY_UNKNOWN_ROOT_HEIGHT:
        case ONUROS_PRIVACY_ROOT_FROM_FUTURE:
        case ONUROS_PRIVACY_ROOT_EXPIRED:
            return PrivateProofError::invalid_effect_binding;
        case ONUROS_PRIVACY_PARAMETER_IDENTITY_MISMATCH:
        case ONUROS_PRIVACY_INVALID_PARAMETERS:
            return PrivateProofError::unsupported_proof_version;
        default:
            return PrivateProofError::backend_unavailable;
        }
    }

public:
    OnurosPrivacyEngineBackend(
        const onuros_privacy_engine_v1* engine, ValidatePayment validate,
        std::vector<onuros_accepted_root_v1> roots,
        std::uint32_t chain_height, std::uint32_t max_root_age)
        : engine_(engine), validate_(validate), roots_(std::move(roots)),
          chain_height_(chain_height), max_root_age_(max_root_age) {}

    VerifiedPrivateEffects verify(
            const TransactionEnvelope& transaction) const override {
        if (engine_ == nullptr || validate_ == nullptr || roots_.empty())
            return {PrivateProofError::backend_unavailable, {}, {}, {}, 0};
        if (transaction.version != onuros_private_payment_envelope_version ||
            transaction.body.size() != onuros_private_payment_bytes)
            return {PrivateProofError::malformed_encoding, {}, {}, {}, 0};

        onuros_validated_payment_v1 result{};
        const auto status = validate_(
            engine_, transaction.body.data(), transaction.body.size(),
            roots_.data(), roots_.size(), chain_height_, max_root_age_,
            &result);
        if (status != ONUROS_PRIVACY_OK)
            return {map_error(status), {}, {}, {}, 0};
        if (result.fee > static_cast<std::uint64_t>(
                             std::numeric_limits<Amount>::max()))
            return {PrivateProofError::invalid_balance, {}, {}, {}, 0};

        const auto root = std::find_if(
            roots_.begin(), roots_.end(), [&result](const auto& candidate) {
                return candidate.height == result.root_height;
            });
        if (root == roots_.end())
            return {PrivateProofError::invalid_effect_binding, {}, {}, {}, 0};

        Hash256 anchor{};
        Hash256 nullifier{};
        std::vector<Hash256> commitments(2U);
        std::copy(std::begin(root->root), std::end(root->root), anchor.begin());
        std::copy(std::begin(result.nullifier), std::end(result.nullifier),
                  nullifier.begin());
        for (std::size_t index = 0U; index < commitments.size(); ++index)
            std::copy(std::begin(result.output_commitments[index]),
                      std::end(result.output_commitments[index]),
                      commitments[index].begin());
        return {PrivateProofError::none, anchor, {nullifier},
                std::move(commitments), static_cast<Amount>(result.fee)};
    }

#ifdef ONUROS_PRIVACY_ENGINE_ENABLED
    static OnurosPrivacyEngineBackend linked(
        const onuros_privacy_engine_v1* engine,
        std::vector<onuros_accepted_root_v1> roots,
        std::uint32_t chain_height, std::uint32_t max_root_age) {
        return {engine,
                &onuros_privacy_engine_validate_payment_with_roots_at_height_v1,
                std::move(roots), chain_height, max_root_age};
    }
#endif
};

} // namespace onuros
