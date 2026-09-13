#include "onuros/compact_private_effect.hpp"

#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint32_t value) {
    check(offset + 4U <= bytes.size());
    for (std::size_t i = 0U; i < 4U; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (8U * i));
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

CompactPrivateEffects fixture() {
    CompactPrivateEffects effects;
    effects.value_balance = 7;
    effects.fee = 7;
    effects.actions.resize(2U);
    effects.actions[0].nullifier.back() = 1U;
    effects.actions[0].encrypted_note.fill(2U);
    effects.actions[1].note_commitment.back() = 3U;
    effects.actions[1].outgoing_ciphertext.fill(4U);
    return effects;
}

} // namespace

int main() {
    check(compact_private_action_effect_size == 308U);
    check(compact_private_effect_encoded_size(1U) == 337U);
    check(compact_private_effect_encoded_size(2U) == 645U);

    CompactPrivateEffects zero_vector;
    zero_vector.actions.resize(2U);
    check(hash_hex(compact_private_effect_digest(zero_vector)) ==
          "de64e1b8d4c1c4e701a0d60db52414d90b96fcc9dacfe490b67fd8ab1b14c0f1");

    const auto original = fixture();
    const auto encoded = encode_compact_private_effects(original);
    check(encoded.size() == 645U);
    const CompactPrivateEffectLimits limits{1'000U, 2U};
    const auto decoded = decode_compact_private_effects(encoded, limits);
    check(decoded.accepted());
    check(decoded.effects->value_balance == original.value_balance);
    check(decoded.effects->fee == original.fee);
    check(decoded.effects->actions.size() == 2U);
    check(decoded.effects->actions[0].nullifier ==
          original.actions[0].nullifier);
    check(decoded.effects->actions[1].outgoing_ciphertext ==
          original.actions[1].outgoing_ciphertext);
    check(encode_compact_private_effects(*decoded.effects) == encoded);

    for (std::size_t length = 0U; length < encoded.size(); ++length) {
        auto truncated = encoded;
        truncated.resize(length);
        check(!decode_compact_private_effects(truncated, limits).accepted());
    }
    auto malformed = encoded;
    malformed.push_back(0U);
    check(decode_compact_private_effects(malformed, {2'000U, 2U}).error ==
          CompactPrivateEffectDecodeError::trailing_bytes);
    malformed = encoded;
    malformed[0] ^= 1U;
    check(decode_compact_private_effects(malformed, limits).error ==
          CompactPrivateEffectDecodeError::invalid_magic);
    malformed = encoded;
    write_u32(malformed, 4U, compact_private_effect_version + 1U);
    check(decode_compact_private_effects(malformed, limits).error ==
          CompactPrivateEffectDecodeError::unsupported_version);
    malformed = encoded;
    malformed[8U] = 0U;
    check(decode_compact_private_effects(malformed, limits).error ==
          CompactPrivateEffectDecodeError::invalid_flags);
    malformed = encoded;
    write_u32(malformed, 25U, 0U);
    check(decode_compact_private_effects(malformed, limits).error ==
          CompactPrivateEffectDecodeError::zero_actions);
    malformed = encoded;
    write_u32(malformed, 25U, 3U);
    check(decode_compact_private_effects(malformed, limits).error ==
          CompactPrivateEffectDecodeError::too_many_actions);
    check(decode_compact_private_effects(encoded, {644U, 2U}).error ==
          CompactPrivateEffectDecodeError::body_too_large);

    auto invalid = original;
    invalid.actions.clear();
    throws_invalid_argument([&invalid] { encode_compact_private_effects(invalid); });
    invalid = original;
    invalid.flags = 0U;
    throws_invalid_argument([&invalid] { encode_compact_private_effects(invalid); });
    invalid = original;
    invalid.fee = -1;
    throws_invalid_argument([&invalid] { encode_compact_private_effects(invalid); });

    auto signed_boundary = original;
    signed_boundary.value_balance = std::numeric_limits<Amount>::min();
    const auto signed_round_trip = decode_compact_private_effects(
        encode_compact_private_effects(signed_boundary), limits);
    check(signed_round_trip.accepted());
    check(signed_round_trip.effects->value_balance ==
          std::numeric_limits<Amount>::min());

    const auto digest = compact_private_effect_digest(original);
    auto changed = original;
    changed.actions[0].encrypted_note[0] ^= 1U;
    check(compact_private_effect_digest(changed) != digest);
    changed = original;
    changed.actions[1].nullifier[0] ^= 1U;
    check(compact_private_effect_digest(changed) != digest);
    return 0;
}
