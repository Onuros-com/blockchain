#include "onuros/private_transaction.hpp"

#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

Hash256 value(std::uint8_t byte) {
    Hash256 hash{};
    hash.back() = byte;
    return hash;
}

PrivateActionBundle action(std::uint8_t seed) {
    PrivateActionBundle result;
    result.value_commitment = value(seed);
    result.nullifier = value(static_cast<std::uint8_t>(seed + 1U));
    result.randomized_key = value(static_cast<std::uint8_t>(seed + 2U));
    result.note_commitment = value(static_cast<std::uint8_t>(seed + 3U));
    result.ephemeral_key = value(static_cast<std::uint8_t>(seed + 4U));
    result.encrypted_note.fill(seed);
    result.outgoing_ciphertext.fill(static_cast<std::uint8_t>(seed + 6U));
    result.spend_authorization.back() = static_cast<std::uint8_t>(seed + 7U);
    return result;
}

PrivateTransactionBundle bundle() {
    PrivateTransactionBundle result;
    result.anchor = value(10U);
    result.value_balance = -5'000;
    result.fee = 7;
    result.actions = {action(20U), action(40U)};
    result.proof.resize(orchard_proof_base_size +
                        2U * orchard_proof_per_action_size, 50U);
    result.binding_signature.back() = 60U;
    return result;
}

PrivateBundleLimits limits() {
    return {20'000U, 4U, 20'000U};
}

template <typename Operation>
void throws_invalid_argument(Operation operation) {
    bool threw = false;
    try {
        operation();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw);
}

void write_u32(std::vector<std::uint8_t>& body, std::size_t offset,
               std::uint32_t value_to_write) {
    check(offset + 4U <= body.size());
    for (std::size_t i = 0; i < 4U; ++i)
        body[offset + i] = static_cast<std::uint8_t>(value_to_write >> (i * 8U));
}

std::size_t proof_size_offset(const PrivateTransactionBundle& candidate) {
    std::size_t offset = 65U;
    for (const auto& item : candidate.actions)
        offset += 160U + item.encrypted_note.size() +
                  item.outgoing_ciphertext.size() + 64U;
    return offset;
}

class ScriptedBackend final : public PrivateProofBackend {
public:
    mutable std::size_t calls = 0U;
    mutable Hash256 observed_signature_digest{};
    PrivateProofError failure = PrivateProofError::none;
    bool mismatch = false;

    VerifiedPrivateEffects verify(
            const PrivateTransactionBundle& candidate,
            const Hash256& signature_digest) const override {
        ++calls;
        observed_signature_digest = signature_digest;
        std::vector<Hash256> nullifiers;
        std::vector<Hash256> commitments;
        for (const auto& item : candidate.actions) {
            nullifiers.push_back(item.nullifier);
            commitments.push_back(item.note_commitment);
        }
        if (mismatch) commitments.back() = value(99U);
        return {failure, candidate.anchor, std::move(nullifiers),
                std::move(commitments), candidate.fee};
    }
};

} // namespace

int main() {
    const auto original = bundle();
    const auto transaction = make_private_transaction(original);
    const auto decoded = decode_private_transaction(transaction, limits());
    check(decoded.accepted());
    check(decoded.bundle->format_version == original.format_version);
    check(decoded.bundle->proof_system_version == original.proof_system_version);
    check(decoded.bundle->anchor == original.anchor);
    check(decoded.bundle->value_balance == original.value_balance);
    check(decoded.bundle->fee == original.fee);
    check(decoded.bundle->actions.size() == original.actions.size());
    check(decoded.bundle->actions[0].nullifier == original.actions[0].nullifier);
    check(decoded.bundle->actions[1].note_commitment ==
          original.actions[1].note_commitment);
    check(decoded.bundle->proof == original.proof);
    check(encode_private_bundle(*decoded.bundle) == transaction.body);

    for (std::size_t length = 0; length < transaction.body.size(); ++length) {
        auto truncated = transaction;
        truncated.body.resize(length);
        check(!decode_private_transaction(truncated, limits()).accepted());
    }

    auto malformed = transaction;
    malformed.body.push_back(0U);
    check(decode_private_transaction(malformed, limits()).error ==
          PrivateBundleDecodeError::trailing_bytes);
    malformed = transaction;
    malformed.body[0] ^= 0xffU;
    check(decode_private_transaction(malformed, limits()).error ==
          PrivateBundleDecodeError::invalid_magic);
    malformed = transaction;
    write_u32(malformed.body, 4U, private_bundle_format_version + 1U);
    check(decode_private_transaction(malformed, limits()).error ==
          PrivateBundleDecodeError::unsupported_format_version);
    malformed = transaction;
    write_u32(malformed.body, 8U, orchard_proof_system_version + 1U);
    check(decode_private_transaction(malformed, limits()).error ==
          PrivateBundleDecodeError::unsupported_proof_version);
    malformed = transaction;
    malformed.body[12U] = 0U;
    check(decode_private_transaction(malformed, limits()).error ==
          PrivateBundleDecodeError::invalid_flags);
    malformed = transaction;
    malformed.body[60U] = 0x80U;
    check(decode_private_transaction(malformed, limits()).error ==
          PrivateBundleDecodeError::invalid_fee);
    malformed = transaction;
    write_u32(malformed.body, 61U, 0U);
    check(decode_private_transaction(malformed, limits()).error ==
          PrivateBundleDecodeError::zero_actions);
    malformed = transaction;
    write_u32(malformed.body, 61U, limits().max_actions + 1U);
    check(decode_private_transaction(malformed, limits()).error ==
          PrivateBundleDecodeError::too_many_actions);
    malformed = transaction;
    write_u32(malformed.body, proof_size_offset(original), 0U);
    check(decode_private_transaction(malformed, limits()).error ==
          PrivateBundleDecodeError::empty_proof);

    auto small = limits();
    small.max_body_bytes = transaction.body.size() - 1U;
    check(decode_private_transaction(transaction, small).error ==
          PrivateBundleDecodeError::body_too_large);
    small = limits();
    small.max_proof_bytes = static_cast<std::uint32_t>(original.proof.size() - 1U);
    check(decode_private_transaction(transaction, small).error ==
          PrivateBundleDecodeError::proof_too_large);
    auto wrong_envelope = transaction;
    wrong_envelope.version = 1U;
    check(decode_private_transaction(wrong_envelope, limits()).error ==
          PrivateBundleDecodeError::unsupported_envelope_version);

    auto invalid = original;
    invalid.fee = -1;
    throws_invalid_argument([&invalid] { encode_private_bundle(invalid); });
    invalid = original;
    invalid.actions.clear();
    throws_invalid_argument([&invalid] { encode_private_bundle(invalid); });
    invalid = original;
    invalid.proof.clear();
    throws_invalid_argument([&invalid] { encode_private_bundle(invalid); });
    invalid = original;
    invalid.flags = 0U;
    throws_invalid_argument([&invalid] { encode_private_bundle(invalid); });

    ScriptedBackend backend;
    CanonicalPrivateTransactionVerifier verifier(backend, limits());
    auto effects = verifier.verify(transaction);
    check(effects.error == PrivateProofError::none);
    check(backend.calls == 1U);
    check(backend.observed_signature_digest ==
          private_signature_digest(original));
    check(backend.observed_signature_digest != transaction_id(transaction));
    check(effects.nullifiers.size() == original.actions.size());

    auto changed_fee = original;
    ++changed_fee.fee;
    check(private_signature_digest(changed_fee) !=
          private_signature_digest(original));
    auto changed_proof = original;
    changed_proof.proof[0] ^= 1U;
    check(private_signature_digest(changed_proof) !=
          private_signature_digest(original));
    auto changed_signatures = original;
    changed_signatures.actions[0].spend_authorization[0] ^= 1U;
    changed_signatures.binding_signature[0] ^= 1U;
    check(private_signature_digest(changed_signatures) ==
          private_signature_digest(original));

    verifier.verify(malformed);
    check(backend.calls == 1U);
    backend.failure = PrivateProofError::invalid_proof;
    check(verifier.verify(transaction).error == PrivateProofError::invalid_proof);
    backend.failure = PrivateProofError::none;
    backend.mismatch = true;
    check(verifier.verify(transaction).error ==
          PrivateProofError::invalid_effect_binding);

    return 0;
}
