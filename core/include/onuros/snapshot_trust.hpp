#pragma once

#include "onuros/state_snapshot.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace onuros {

inline constexpr std::array<std::uint8_t, 24> snapshot_signature_domain{
    'O', 'N', 'U', 'R', 'O', 'S', '_', 'S', 'N', 'A', 'P', 'S', 'H', 'O',
    'T', '_', 'S', 'I', 'G', '_', 'V', '1', 0U, 0U};

struct SnapshotSigner {
    std::uint32_t identifier = 0U;
    std::array<std::uint8_t, 32> public_key{};
    bool offline_security_key = false;
};

struct SnapshotSignature {
    std::uint32_t signer_identifier = 0U;
    std::array<std::uint8_t, 64> signature{};
};

struct SnapshotThresholdPolicy {
    std::uint32_t threshold = 0U;
    bool require_offline_security_key = true;
    std::vector<SnapshotSigner> signers;
};

enum class SnapshotTrustError {
    none,
    invalid_policy,
    unknown_signer,
    duplicate_signer,
    invalid_signature,
    offline_security_signature_missing,
    threshold_not_met
};

struct SnapshotTrustResult {
    SnapshotTrustError error = SnapshotTrustError::none;
    std::vector<std::uint32_t> valid_signers;

    bool authenticated() const noexcept {
        return error == SnapshotTrustError::none;
    }
};

inline std::vector<std::uint8_t> snapshot_signature_message(
        const std::vector<std::uint8_t>& encoded_manifest) {
    const auto manifest_id = state_snapshot_manifest_id(encoded_manifest);
    std::vector<std::uint8_t> message(snapshot_signature_domain.begin(),
                                      snapshot_signature_domain.end());
    message.insert(message.end(), manifest_id.begin(), manifest_id.end());
    return message;
}

namespace snapshot_trust_detail {

struct EvpKeyDeleter {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
};

struct EvpContextDeleter {
    void operator()(EVP_MD_CTX* context) const noexcept {
        EVP_MD_CTX_free(context);
    }
};

inline bool verify_ed25519(
        const std::array<std::uint8_t, 32>& public_key,
        const std::array<std::uint8_t, 64>& signature,
        const std::vector<std::uint8_t>& message) {
    const std::unique_ptr<EVP_PKEY, EvpKeyDeleter> key(
        EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                    public_key.data(), public_key.size()));
    if (!key) return false;
    const std::unique_ptr<EVP_MD_CTX, EvpContextDeleter> context(
        EVP_MD_CTX_new());
    if (!context ||
        EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr,
                             key.get()) != 1)
        return false;
    return EVP_DigestVerify(context.get(), signature.data(), signature.size(),
                            message.data(), message.size()) == 1;
}

} // namespace snapshot_trust_detail

inline SnapshotTrustResult verify_snapshot_threshold(
        const std::vector<std::uint8_t>& encoded_manifest,
        const std::vector<SnapshotSignature>& signatures,
        const SnapshotThresholdPolicy& policy) {
    if (policy.threshold == 0U || policy.signers.empty() ||
        policy.threshold > policy.signers.size())
        return {SnapshotTrustError::invalid_policy, {}};
    std::set<std::uint32_t> configured_identifiers;
    std::set<std::array<std::uint8_t, 32>> configured_keys;
    for (const auto& signer : policy.signers)
        if (signer.identifier == 0U ||
            !configured_identifiers.insert(signer.identifier).second ||
            !configured_keys.insert(signer.public_key).second)
            return {SnapshotTrustError::invalid_policy, {}};

    const auto message = snapshot_signature_message(encoded_manifest);
    SnapshotTrustResult result;
    std::set<std::uint32_t> seen;
    bool offline_security_signed = false;
    for (const auto& signature : signatures) {
        const auto signer = std::find_if(
            policy.signers.begin(), policy.signers.end(),
            [&](const SnapshotSigner& candidate) {
                return candidate.identifier == signature.signer_identifier;
            });
        if (signer == policy.signers.end())
            return {SnapshotTrustError::unknown_signer, {}};
        if (!seen.insert(signature.signer_identifier).second)
            return {SnapshotTrustError::duplicate_signer, {}};
        if (!snapshot_trust_detail::verify_ed25519(
                signer->public_key, signature.signature, message))
            return {SnapshotTrustError::invalid_signature, {}};
        result.valid_signers.push_back(signature.signer_identifier);
        offline_security_signed = offline_security_signed ||
                                  signer->offline_security_key;
    }
    if (result.valid_signers.size() < policy.threshold)
        return {SnapshotTrustError::threshold_not_met,
                std::move(result.valid_signers)};
    if (policy.require_offline_security_key && !offline_security_signed)
        return {SnapshotTrustError::offline_security_signature_missing,
                std::move(result.valid_signers)};
    return result;
}

struct ThresholdSnapshotImportResult {
    SnapshotTrustError trust_error = SnapshotTrustError::none;
    StateSnapshotError snapshot_error = StateSnapshotError::none;

    bool imported() const noexcept {
        return trust_error == SnapshotTrustError::none &&
               snapshot_error == StateSnapshotError::none;
    }
};

inline ThresholdSnapshotImportResult import_threshold_authenticated_snapshot(
        const std::filesystem::path& destination,
        const std::vector<std::uint8_t>& encoded_manifest,
        const std::vector<std::uint8_t>& content,
        const std::vector<SnapshotSignature>& signatures,
        const SnapshotThresholdPolicy& trust_policy,
        const PruningCheckpoint& expected_checkpoint,
        const Hash256& expected_consensus_parameters_hash,
        std::size_t maximum_entries) {
    const auto trust = verify_snapshot_threshold(
        encoded_manifest, signatures, trust_policy);
    if (!trust.authenticated()) return {trust.error, StateSnapshotError::none};
    return {SnapshotTrustError::none,
            import_authenticated_shielded_snapshot(
                destination, encoded_manifest, content,
                state_snapshot_manifest_id(encoded_manifest),
                expected_checkpoint, expected_consensus_parameters_hash,
                maximum_entries)};
}

} // namespace onuros
