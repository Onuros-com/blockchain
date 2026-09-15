#pragma once

#include "onuros/private_admission.hpp"
#include "onuros/reward_policy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace onuros {

inline constexpr std::array<std::uint8_t, 4> private_reward_magic{
    'O', 'N', 'R', '1'};
inline constexpr std::uint32_t private_reward_format_version = 1U;
// The fresh Groth16/Poseidon testnet uses one envelope version for reward and
// ordinary private-payment records. Version 2 remains historical Orchard data.
inline constexpr std::uint32_t private_reward_envelope_version = 3U;

struct PrivateRewardClaim {
    EcosystemReward amounts{};
    Hash256 miner_recipient{};
    Hash256 team_recipient{};
    Hash256 ecosystem_recipient{};
};

enum class PrivateRewardError {
    none,
    malformed,
    invalid_amount,
    invalid_allocation,
    missing_miner_recipient,
    wrong_team_recipient,
    wrong_ecosystem_recipient,
    noncanonical_empty_recipient
};

struct PrivateRewardResult {
    PrivateRewardError error = PrivateRewardError::malformed;
    std::optional<PrivateRewardClaim> claim;
    bool accepted() const {
        return error == PrivateRewardError::none && claim.has_value();
    }
};

inline bool is_zero_hash(const Hash256& hash) {
    return hash == Hash256{};
}

inline std::vector<std::uint8_t> encode_private_reward(
        const PrivateRewardClaim& claim) {
    if (claim.amounts.miner < 0 || claim.amounts.team < 0 ||
        claim.amounts.ecosystem < 0)
        throw std::invalid_argument("negative private reward");
    std::vector<std::uint8_t> output(private_reward_magic.begin(),
                                     private_reward_magic.end());
    detail::append_little_endian(output, private_reward_format_version);
    detail::append_little_endian(
        output, static_cast<std::uint64_t>(claim.amounts.miner));
    detail::append_little_endian(
        output, static_cast<std::uint64_t>(claim.amounts.team));
    detail::append_little_endian(
        output, static_cast<std::uint64_t>(claim.amounts.ecosystem));
    detail::append_hash(output, claim.miner_recipient);
    detail::append_hash(output, claim.team_recipient);
    detail::append_hash(output, claim.ecosystem_recipient);
    return output;
}

inline TransactionEnvelope make_private_reward_transaction(
        const PrivateRewardClaim& claim) {
    return {private_reward_envelope_version,
            encode_private_reward(claim)};
}

inline PrivateRewardResult decode_private_reward(
        const TransactionEnvelope& transaction) {
    constexpr std::size_t encoded_size = 4U + 4U + 24U + 96U;
    if (transaction.version != private_reward_envelope_version ||
        transaction.body.size() != encoded_size)
        return {PrivateRewardError::malformed, std::nullopt};
    detail::ByteReader reader(transaction.body);
    std::array<std::uint8_t, 4> magic{};
    for (auto& byte : magic)
        if (!reader.read_little_endian(byte))
            return {PrivateRewardError::malformed, std::nullopt};
    std::uint32_t version = 0U;
    if (magic != private_reward_magic ||
        !reader.read_little_endian(version) ||
        version != private_reward_format_version)
        return {PrivateRewardError::malformed, std::nullopt};
    std::uint64_t miner = 0U;
    std::uint64_t team = 0U;
    std::uint64_t ecosystem = 0U;
    PrivateRewardClaim claim;
    if (!reader.read_little_endian(miner) ||
        !reader.read_little_endian(team) ||
        !reader.read_little_endian(ecosystem) ||
        miner > static_cast<std::uint64_t>(std::numeric_limits<Amount>::max()) ||
        team > static_cast<std::uint64_t>(std::numeric_limits<Amount>::max()) ||
        ecosystem >
            static_cast<std::uint64_t>(std::numeric_limits<Amount>::max()) ||
        !reader.read_hash(claim.miner_recipient) ||
        !reader.read_hash(claim.team_recipient) ||
        !reader.read_hash(claim.ecosystem_recipient) || !reader.exhausted())
        return {PrivateRewardError::invalid_amount, std::nullopt};
    claim.amounts = {static_cast<Amount>(miner), static_cast<Amount>(team),
                     static_cast<Amount>(ecosystem)};
    return {PrivateRewardError::none,
            std::optional<PrivateRewardClaim>{claim}};
}

class PrivateRewardPolicy {
    OnurosRewardPolicy economics_;
    Hash256 team_recipient_{};
    Hash256 ecosystem_recipient_{};
public:
    PrivateRewardPolicy(Hash256 team_recipient,
                        Hash256 ecosystem_recipient)
        : team_recipient_(team_recipient),
          ecosystem_recipient_(ecosystem_recipient) {
        if (is_zero_hash(team_recipient_) ||
            is_zero_hash(ecosystem_recipient_) ||
            team_recipient_ == ecosystem_recipient_)
            throw std::invalid_argument("invalid fixed reward recipients");
    }

    PrivateRewardError validate(
            Height height, const PrivateBlockAdmission::Prepared& prepared,
            const TransactionEnvelope& reward_transaction) const {
        const auto decoded = decode_private_reward(reward_transaction);
        if (!decoded.accepted()) return decoded.error;
        const auto& claim = *decoded.claim;
        if (!economics_.validate(height, prepared.fees(), claim.amounts))
            return PrivateRewardError::invalid_allocation;
        if (claim.amounts.miner != 0 && is_zero_hash(claim.miner_recipient))
            return PrivateRewardError::missing_miner_recipient;
        if (claim.amounts.miner == 0 && !is_zero_hash(claim.miner_recipient))
            return PrivateRewardError::noncanonical_empty_recipient;
        if (claim.amounts.team != 0 &&
            claim.team_recipient != team_recipient_)
            return PrivateRewardError::wrong_team_recipient;
        if (claim.amounts.team == 0 && !is_zero_hash(claim.team_recipient))
            return PrivateRewardError::noncanonical_empty_recipient;
        if (claim.amounts.ecosystem != 0 &&
            claim.ecosystem_recipient != ecosystem_recipient_)
            return PrivateRewardError::wrong_ecosystem_recipient;
        if (claim.amounts.ecosystem == 0 &&
            !is_zero_hash(claim.ecosystem_recipient))
            return PrivateRewardError::noncanonical_empty_recipient;
        return PrivateRewardError::none;
    }
};

} // namespace onuros
