#include "onuros/private_reward.hpp"

#include <cstdlib>
#include <limits>

namespace {
using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

Hash256 value(std::uint8_t byte) {
    Hash256 result{};
    result.back() = byte;
    return result;
}

class Verifier final : public PrivateTransactionVerifier {
public:
    VerifiedPrivateEffects verify(
            const TransactionEnvelope&) const override {
        return {PrivateProofError::none, value(1U), {value(2U)},
                {value(3U)}, 17};
    }
};

PrivateBlockAdmission::Prepared prepared() {
    ShieldedState state(value(4U), value(1U));
    Verifier verifier;
    const auto result = PrivateBlockAdmission::prepare(
        state, state.tip(), {{1U, {1U}}}, verifier,
        {1024U, 2U, 2U, 2U});
    check(result.accepted());
    return *result.prepared;
}
}

int main() {
    const auto team = value(10U);
    const auto ecosystem = value(11U);
    const auto miner = value(12U);
    PrivateRewardPolicy policy(team, ecosystem);
    const auto effects = prepared();
    OnurosRewardPolicy economics;
    auto amounts = economics.allocate(1U, effects.fees(), 1 * atomic_units_per_coin);
    PrivateRewardClaim claim{amounts, miner, team, ecosystem};
    const auto encoded = make_private_reward_transaction(claim);
    check(policy.validate(1U, effects, encoded) == PrivateRewardError::none);
    const auto decoded = decode_private_reward(encoded);
    check(decoded.accepted() && decoded.claim->amounts.miner == amounts.miner &&
          decoded.claim->team_recipient == team);

    auto changed = claim;
    ++changed.amounts.miner;
    check(policy.validate(1U, effects, make_private_reward_transaction(changed)) ==
          PrivateRewardError::invalid_allocation);
    changed = claim;
    changed.team_recipient = value(99U);
    check(policy.validate(1U, effects, make_private_reward_transaction(changed)) ==
          PrivateRewardError::wrong_team_recipient);
    changed = claim;
    changed.ecosystem_recipient = value(99U);
    check(policy.validate(1U, effects, make_private_reward_transaction(changed)) ==
          PrivateRewardError::wrong_ecosystem_recipient);
    changed = claim;
    changed.miner_recipient = {};
    check(policy.validate(1U, effects, make_private_reward_transaction(changed)) ==
          PrivateRewardError::missing_miner_recipient);

    auto malformed = encoded;
    malformed.body.push_back(0U);
    check(policy.validate(1U, effects, malformed) ==
          PrivateRewardError::malformed);
    malformed = encoded;
    malformed.body[0] ^= 1U;
    check(policy.validate(1U, effects, malformed) ==
          PrivateRewardError::malformed);
    malformed = encoded;
    malformed.body[15U] = 0x80U;
    check(!decode_private_reward(malformed).accepted());

    bool rejected = false;
    try {
        make_private_reward_transaction(
            {{-1, 0, 0}, miner, {}, {}});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected);
    return 0;
}
