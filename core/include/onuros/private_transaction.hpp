#pragma once

#include "onuros/private_admission.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace onuros {

static_assert(sizeof(Amount) == sizeof(std::uint64_t) &&
              std::numeric_limits<Amount>::is_signed,
              "private value balance requires a signed 64-bit Amount");

inline constexpr std::uint32_t private_transaction_envelope_version = 2U;
inline constexpr std::uint32_t private_bundle_format_version = 2U;
inline constexpr std::uint32_t orchard_proof_system_version = 1U;
inline constexpr std::uint8_t orchard_enabled_flags = 0x03U;
inline constexpr std::size_t orchard_encrypted_note_size = 580U;
inline constexpr std::size_t orchard_outgoing_ciphertext_size = 80U;
inline constexpr std::size_t orchard_proof_base_size = 2720U;
inline constexpr std::size_t orchard_proof_per_action_size = 2272U;
inline constexpr std::size_t private_bundle_magic_size = 4U;
inline constexpr std::size_t private_bundle_format_version_size = 4U;
inline constexpr std::size_t private_proof_system_version_size = 4U;
inline constexpr std::size_t private_bundle_flags_size = 1U;
inline constexpr std::size_t private_anchor_size = 32U;
inline constexpr std::size_t private_value_balance_size = 8U;
inline constexpr std::size_t private_fee_size = 8U;
inline constexpr std::size_t private_action_count_size = 4U;
inline constexpr std::size_t private_bundle_fixed_header_size =
    private_bundle_magic_size + private_bundle_format_version_size +
    private_proof_system_version_size + private_bundle_flags_size +
    private_anchor_size + private_value_balance_size + private_fee_size +
    private_action_count_size;
inline constexpr std::size_t private_action_core_size = 160U;
inline constexpr std::size_t private_action_signature_size = 64U;
inline constexpr std::size_t private_action_encoded_size =
    private_action_core_size + orchard_encrypted_note_size +
    orchard_outgoing_ciphertext_size + private_action_signature_size;
inline constexpr std::size_t private_proof_length_size = 4U;
inline constexpr std::size_t private_binding_signature_size = 64U;
inline constexpr std::size_t transaction_envelope_overhead_size = 8U;
inline constexpr std::size_t transaction_batch_count_size = 4U;

struct PrivateTransactionByteLayout {
    std::size_t bundle_magic = private_bundle_magic_size;
    std::size_t format_version = private_bundle_format_version_size;
    std::size_t proof_system_version = private_proof_system_version_size;
    std::size_t flags = private_bundle_flags_size;
    std::size_t anchor = private_anchor_size;
    std::size_t value_balance = private_value_balance_size;
    std::size_t fee = private_fee_size;
    std::size_t action_count = private_action_count_size;
    std::size_t fixed_header = private_bundle_fixed_header_size;
    std::size_t action_core = 0U;
    std::size_t encrypted_notes = 0U;
    std::size_t outgoing_ciphertexts = 0U;
    std::size_t spend_authorizations = 0U;
    std::size_t proof_length = private_proof_length_size;
    std::size_t proof = 0U;
    std::size_t binding_signature = private_binding_signature_size;
    std::size_t body = 0U;
    std::size_t transaction_envelope = 0U;
    std::size_t single_transaction_batch = 0U;
};

inline PrivateTransactionByteLayout private_transaction_byte_layout(
        std::size_t action_count) {
    if (action_count == 0U)
        throw std::invalid_argument("private byte layout requires an action");
    constexpr auto per_action = private_action_encoded_size +
                                orchard_proof_per_action_size;
    constexpr auto fixed = private_bundle_fixed_header_size +
                           private_proof_length_size +
                           orchard_proof_base_size +
                           private_binding_signature_size;
    constexpr auto outer = transaction_envelope_overhead_size +
                           transaction_batch_count_size;
    if (action_count >
        (std::numeric_limits<std::size_t>::max() - fixed - outer) /
            per_action)
        throw std::length_error("private byte layout exceeds size_t");

    PrivateTransactionByteLayout layout;
    layout.action_core = private_action_core_size * action_count;
    layout.encrypted_notes = orchard_encrypted_note_size * action_count;
    layout.outgoing_ciphertexts =
        orchard_outgoing_ciphertext_size * action_count;
    layout.spend_authorizations =
        private_action_signature_size * action_count;
    layout.proof = orchard_proof_base_size +
                   orchard_proof_per_action_size * action_count;
    layout.body = fixed + per_action * action_count;
    layout.transaction_envelope =
        transaction_envelope_overhead_size + layout.body;
    layout.single_transaction_batch =
        transaction_batch_count_size + layout.transaction_envelope;
    return layout;
}

struct PrivateBundleLimits {
    std::size_t max_body_bytes;
    std::uint32_t max_actions;
    std::uint32_t max_proof_bytes;
};

struct PrivateActionBundle {
    Hash256 value_commitment{};
    Hash256 nullifier{};
    Hash256 randomized_key{};
    Hash256 note_commitment{};
    Hash256 ephemeral_key{};
    std::array<std::uint8_t, orchard_encrypted_note_size> encrypted_note{};
    std::array<std::uint8_t, orchard_outgoing_ciphertext_size>
        outgoing_ciphertext{};
    std::array<std::uint8_t, 64> spend_authorization{};
};

struct PrivateTransactionBundle {
    std::uint32_t format_version = private_bundle_format_version;
    std::uint32_t proof_system_version = orchard_proof_system_version;
    std::uint8_t flags = orchard_enabled_flags;
    Hash256 anchor{};
    // Signed net value leaving the Orchard pool. This is a proof input and is
    // distinct from the non-negative ONUROS transaction fee.
    Amount value_balance = 0;
    Amount fee = 0;
    std::vector<PrivateActionBundle> actions;
    std::vector<std::uint8_t> proof;
    std::array<std::uint8_t, 64> binding_signature{};
};

enum class PrivateBundleDecodeError {
    none,
    body_too_large,
    unsupported_envelope_version,
    invalid_magic,
    unsupported_format_version,
    unsupported_proof_version,
    invalid_flags,
    invalid_fee,
    zero_actions,
    too_many_actions,
    proof_too_large,
    non_canonical_proof_size,
    empty_proof,
    truncated,
    trailing_bytes
};

struct PrivateBundleDecodeResult {
    PrivateBundleDecodeError error = PrivateBundleDecodeError::truncated;
    std::optional<PrivateTransactionBundle> bundle;

    bool accepted() const {
        return error == PrivateBundleDecodeError::none && bundle.has_value();
    }
};

namespace private_detail {

inline constexpr std::array<std::uint8_t, 4> bundle_magic{
    0x4fU, 0x4eU, 0x50U, 0x32U // "ONP2"
};

template <typename Integer>
inline void append_little(std::vector<std::uint8_t>& output, Integer value) {
    for (std::size_t i = 0; i < sizeof(Integer); ++i)
        output.push_back(static_cast<std::uint8_t>(value >> (i * 8U)));
}

inline void append_hash(std::vector<std::uint8_t>& output,
                        const Hash256& hash) {
    output.insert(output.end(), hash.begin(), hash.end());
}

template <std::size_t Size>
inline void append_array(std::vector<std::uint8_t>& output,
                         const std::array<std::uint8_t, Size>& bytes) {
    output.insert(output.end(), bytes.begin(), bytes.end());
}

inline void append_sized(std::vector<std::uint8_t>& output,
                         const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("private field exceeds canonical length");
    append_little(output, static_cast<std::uint32_t>(bytes.size()));
    output.insert(output.end(), bytes.begin(), bytes.end());
}

class Reader {
    const std::vector<std::uint8_t>& input_;
    std::size_t position_ = 0U;

public:
    explicit Reader(const std::vector<std::uint8_t>& input) : input_(input) {}

    std::size_t remaining() const { return input_.size() - position_; }
    bool exhausted() const { return position_ == input_.size(); }

    template <typename Integer>
    bool read_little(Integer& value) {
        if (remaining() < sizeof(Integer)) return false;
        value = 0;
        for (std::size_t i = 0; i < sizeof(Integer); ++i)
            value |= static_cast<Integer>(input_[position_ + i]) << (i * 8U);
        position_ += sizeof(Integer);
        return true;
    }

    template <std::size_t Size>
    bool read_array(std::array<std::uint8_t, Size>& output) {
        if (remaining() < Size) return false;
        for (std::size_t i = 0; i < Size; ++i)
            output[i] = input_[position_ + i];
        position_ += Size;
        return true;
    }

    bool read_hash(Hash256& output) { return read_array(output); }

    bool read_bytes(std::size_t size, std::vector<std::uint8_t>& output) {
        if (size > remaining()) return false;
        output.assign(input_.begin() + static_cast<std::ptrdiff_t>(position_),
                      input_.begin() +
                          static_cast<std::ptrdiff_t>(position_ + size));
        position_ += size;
        return true;
    }
};

inline PrivateBundleDecodeError read_sized_proof(
        Reader& reader, std::uint32_t maximum,
        std::size_t expected_size, std::vector<std::uint8_t>& output) {
    std::uint32_t size = 0U;
    if (!reader.read_little(size)) return PrivateBundleDecodeError::truncated;
    if (size == 0U)
        return PrivateBundleDecodeError::empty_proof;
    if (size > maximum) return PrivateBundleDecodeError::proof_too_large;
    if (size != expected_size)
        return PrivateBundleDecodeError::non_canonical_proof_size;
    if (!reader.read_bytes(size, output))
        return PrivateBundleDecodeError::truncated;
    return PrivateBundleDecodeError::none;
}

} // namespace private_detail

inline std::vector<std::uint8_t> encode_private_bundle(
        const PrivateTransactionBundle& bundle) {
    if (bundle.format_version != private_bundle_format_version)
        throw std::invalid_argument("unsupported private bundle format");
    if (bundle.proof_system_version != orchard_proof_system_version)
        throw std::invalid_argument("unsupported private proof version");
    if (bundle.flags != orchard_enabled_flags)
        throw std::invalid_argument("unsupported private bundle flags");
    if (bundle.fee < 0)
        throw std::invalid_argument("negative private transaction fee");
    if (bundle.actions.empty())
        throw std::invalid_argument("private transaction has no actions");
    if (bundle.actions.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("too many private actions");
    if (bundle.proof.empty())
        throw std::invalid_argument("private transaction has no proof");
    if (bundle.actions.size() >
            (std::numeric_limits<std::size_t>::max() -
             orchard_proof_base_size) /
                orchard_proof_per_action_size ||
        bundle.proof.size() !=
            orchard_proof_base_size + orchard_proof_per_action_size *
                                          bundle.actions.size())
        throw std::invalid_argument("non-canonical Orchard proof size");

    const auto layout = private_transaction_byte_layout(bundle.actions.size());
    std::vector<std::uint8_t> output;
    output.reserve(layout.body);
    output.insert(output.end(), private_detail::bundle_magic.begin(),
                  private_detail::bundle_magic.end());
    private_detail::append_little(output, bundle.format_version);
    private_detail::append_little(output, bundle.proof_system_version);
    output.push_back(bundle.flags);
    private_detail::append_hash(output, bundle.anchor);
    private_detail::append_little(
        output, static_cast<std::uint64_t>(bundle.value_balance));
    private_detail::append_little(
        output, static_cast<std::uint64_t>(bundle.fee));
    private_detail::append_little(
        output, static_cast<std::uint32_t>(bundle.actions.size()));
    for (const auto& action : bundle.actions) {
        private_detail::append_hash(output, action.value_commitment);
        private_detail::append_hash(output, action.nullifier);
        private_detail::append_hash(output, action.randomized_key);
        private_detail::append_hash(output, action.note_commitment);
        private_detail::append_hash(output, action.ephemeral_key);
        private_detail::append_array(output, action.encrypted_note);
        private_detail::append_array(output, action.outgoing_ciphertext);
        private_detail::append_array(output, action.spend_authorization);
    }
    private_detail::append_sized(output, bundle.proof);
    private_detail::append_array(output, bundle.binding_signature);
    if (output.size() != layout.body)
        throw std::logic_error("private byte layout mismatch");
    return output;
}

inline TransactionEnvelope make_private_transaction(
        const PrivateTransactionBundle& bundle) {
    return {private_transaction_envelope_version,
            encode_private_bundle(bundle)};
}

// The Orchard spend and binding signatures authorize this digest. It commits
// to the entire canonical private body (including fee and proof) with only the
// signature fields zeroed, avoiding a circular dependency on the final txid.
inline Hash256 private_signature_digest(
        const PrivateTransactionBundle& bundle) {
    auto encoded = encode_private_bundle(bundle);
    constexpr std::size_t fixed_prefix_size =
        private_bundle_fixed_header_size;
    constexpr std::size_t action_size = private_action_encoded_size;
    constexpr std::size_t signature_offset_in_action =
        action_size - 64U;
    for (std::size_t i = 0; i < bundle.actions.size(); ++i) {
        const auto offset = fixed_prefix_size + i * action_size +
                            signature_offset_in_action;
        std::fill(encoded.begin() + static_cast<std::ptrdiff_t>(offset),
                  encoded.begin() + static_cast<std::ptrdiff_t>(offset + 64U),
                  std::uint8_t{0});
    }
    std::fill(encoded.end() - 64, encoded.end(), std::uint8_t{0});
    constexpr std::array<std::uint8_t, 22> domain{
        'O', 'n', 'u', 'r', 'o', 's', 'P', 'r', 'i', 'v', 'a', 't', 'e',
        'S', 'i', 'g', 'H', 'a', 's', 'h', 'V', '2'};
    std::vector<std::uint8_t> preimage(domain.begin(), domain.end());
    preimage.insert(preimage.end(), encoded.begin(), encoded.end());
    return double_sha256(preimage);
}

inline PrivateBundleDecodeResult decode_private_transaction(
        const TransactionEnvelope& transaction,
        const PrivateBundleLimits& limits) {
    using Error = PrivateBundleDecodeError;
    if (transaction.body.size() > limits.max_body_bytes)
        return {Error::body_too_large, std::nullopt};
    if (transaction.version != private_transaction_envelope_version)
        return {Error::unsupported_envelope_version, std::nullopt};

    private_detail::Reader reader(transaction.body);
    std::array<std::uint8_t, 4> magic{};
    if (!reader.read_array(magic)) return {Error::truncated, std::nullopt};
    if (magic != private_detail::bundle_magic)
        return {Error::invalid_magic, std::nullopt};

    PrivateTransactionBundle bundle;
    if (!reader.read_little(bundle.format_version))
        return {Error::truncated, std::nullopt};
    if (bundle.format_version != private_bundle_format_version)
        return {Error::unsupported_format_version, std::nullopt};
    if (!reader.read_little(bundle.proof_system_version))
        return {Error::truncated, std::nullopt};
    if (bundle.proof_system_version != orchard_proof_system_version)
        return {Error::unsupported_proof_version, std::nullopt};
    if (!reader.read_little(bundle.flags))
        return {Error::truncated, std::nullopt};
    if (bundle.flags != orchard_enabled_flags)
        return {Error::invalid_flags, std::nullopt};
    if (!reader.read_hash(bundle.anchor))
        return {Error::truncated, std::nullopt};

    std::uint64_t encoded_value_balance = 0U;
    if (!reader.read_little(encoded_value_balance))
        return {Error::truncated, std::nullopt};
    if (encoded_value_balance <=
        static_cast<std::uint64_t>(std::numeric_limits<Amount>::max())) {
        bundle.value_balance = static_cast<Amount>(encoded_value_balance);
    } else {
        const auto magnitude = (~encoded_value_balance) + 1U;
        if (magnitude == (std::uint64_t{1} << 63U))
            bundle.value_balance = std::numeric_limits<Amount>::min();
        else
            bundle.value_balance = -static_cast<Amount>(magnitude);
    }

    std::uint64_t encoded_fee = 0U;
    if (!reader.read_little(encoded_fee))
        return {Error::truncated, std::nullopt};
    if (encoded_fee >
        static_cast<std::uint64_t>(std::numeric_limits<Amount>::max()))
        return {Error::invalid_fee, std::nullopt};
    bundle.fee = static_cast<Amount>(encoded_fee);

    std::uint32_t action_count = 0U;
    if (!reader.read_little(action_count))
        return {Error::truncated, std::nullopt};
    if (action_count == 0U) return {Error::zero_actions, std::nullopt};
    if (action_count > limits.max_actions)
        return {Error::too_many_actions, std::nullopt};
    constexpr std::size_t minimum_action_bytes = private_action_encoded_size;
    if (action_count > reader.remaining() / minimum_action_bytes)
        return {Error::truncated, std::nullopt};

    bundle.actions.reserve(action_count);
    for (std::uint32_t i = 0; i < action_count; ++i) {
        PrivateActionBundle action;
        if (!reader.read_hash(action.value_commitment) ||
            !reader.read_hash(action.nullifier) ||
            !reader.read_hash(action.randomized_key) ||
            !reader.read_hash(action.note_commitment) ||
            !reader.read_hash(action.ephemeral_key))
            return {Error::truncated, std::nullopt};
        if (!reader.read_array(action.encrypted_note) ||
            !reader.read_array(action.outgoing_ciphertext))
            return {Error::truncated, std::nullopt};
        if (!reader.read_array(action.spend_authorization))
            return {Error::truncated, std::nullopt};
        bundle.actions.push_back(std::move(action));
    }

    if (action_count >
        (std::numeric_limits<std::size_t>::max() -
         orchard_proof_base_size) /
            orchard_proof_per_action_size)
        return {Error::proof_too_large, std::nullopt};
    const auto expected_proof_size =
        orchard_proof_base_size + orchard_proof_per_action_size * action_count;
    auto error = private_detail::read_sized_proof(
        reader, limits.max_proof_bytes, expected_proof_size, bundle.proof);
    if (error != Error::none) return {error, std::nullopt};
    if (!reader.read_array(bundle.binding_signature))
        return {Error::truncated, std::nullopt};
    if (!reader.exhausted()) return {Error::trailing_bytes, std::nullopt};
    return {Error::none,
            std::optional<PrivateTransactionBundle>{std::move(bundle)}};
}

class PrivateProofBackend {
public:
    virtual ~PrivateProofBackend() = default;
    virtual VerifiedPrivateEffects verify(
        const PrivateTransactionBundle& bundle,
        const Hash256& signature_digest) const = 0;
};

class CanonicalPrivateTransactionVerifier final
    : public PrivateTransactionVerifier {
    const PrivateProofBackend& backend_;
    PrivateBundleLimits limits_;

public:
    CanonicalPrivateTransactionVerifier(const PrivateProofBackend& backend,
                                        PrivateBundleLimits limits)
        : backend_(backend), limits_(limits) {}

    VerifiedPrivateEffects verify(
            const TransactionEnvelope& transaction) const override {
        const auto decoded = decode_private_transaction(transaction, limits_);
        if (!decoded.accepted()) {
            const auto proof_error =
                decoded.error ==
                        PrivateBundleDecodeError::unsupported_proof_version
                    ? PrivateProofError::unsupported_proof_version
                    : PrivateProofError::malformed_encoding;
            return {proof_error, {}, {}, {}, 0};
        }

        const auto& bundle = *decoded.bundle;
        // Stage 6 has no transparent value pool. Every unit leaving Orchard
        // must therefore be the transaction fee; negative or unmatched value
        // balance would otherwise create value outside the reward path.
        if (bundle.value_balance != bundle.fee)
            return {PrivateProofError::invalid_balance, {}, {}, {}, 0};
        auto effects = backend_.verify(bundle, private_signature_digest(bundle));
        if (effects.error != PrivateProofError::none) return effects;

        std::vector<Hash256> nullifiers;
        std::vector<Hash256> commitments;
        nullifiers.reserve(bundle.actions.size());
        commitments.reserve(bundle.actions.size());
        for (const auto& action : bundle.actions) {
            nullifiers.push_back(action.nullifier);
            commitments.push_back(action.note_commitment);
        }
        if (effects.anchor != bundle.anchor || effects.fee != bundle.fee ||
            effects.nullifiers != nullifiers ||
            effects.commitments != commitments)
            return {PrivateProofError::invalid_effect_binding, {}, {}, {}, 0};
        return effects;
    }
};

} // namespace onuros
