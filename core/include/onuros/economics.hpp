#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace onuros {
using Amount = std::int64_t;
using Height = std::uint64_t;

inline Amount checked_add(Amount a, Amount b) {
    if (a < 0 || b < 0 || a > std::numeric_limits<Amount>::max() - b)
        throw std::overflow_error("invalid or overflowing amount");
    return a + b;
}
inline Amount checked_multiply(Amount value, Height count) {
    if (value < 0) throw std::invalid_argument("negative amount");
    if (value != 0 && count > static_cast<Height>(std::numeric_limits<Amount>::max() / value))
        throw std::overflow_error("issuance exceeds accounting representation");
    return value * static_cast<Amount>(value == 0 ? 0 : count);
}

// Cumulative issuance uses two 64-bit limbs. A positive signed-64 reward
// times at most UINT64_MAX blocks is strictly below 2^127.
// This is an in-memory accounting type, not a consensus wire encoding.
struct Issuance {
    std::uint64_t high = 0;
    std::uint64_t low = 0;
};
inline bool operator==(Issuance a, Issuance b) {
    return a.high == b.high && a.low == b.low;
}
inline Issuance add_issuance(Issuance a, Issuance b) {
    const auto low = a.low + b.low;
    const std::uint64_t carry = low < a.low ? 1 : 0;
    const auto max = std::numeric_limits<std::uint64_t>::max();
    if (a.high > max - b.high) throw std::overflow_error("issuance addition overflow");
    const auto high = a.high + b.high;
    if (high > max - carry) throw std::overflow_error("issuance carry overflow");
    return {high + carry, low};
}
// Arithmetic undo only: chain integration must supply the correct undo amount.
inline Issuance subtract_issuance(Issuance total, Issuance amount) {
    if (total.high < amount.high ||
        (total.high == amount.high && total.low < amount.low))
        throw std::underflow_error("issuance subtraction underflow");
    const std::uint64_t borrow = total.low < amount.low ? 1 : 0;
    return {total.high - amount.high - borrow, total.low - amount.low};
}
inline Issuance multiply_issuance(Amount value, Height count) {
    if (value < 0) throw std::invalid_argument("negative issuance");
    Issuance result{};
    Issuance term{0, static_cast<std::uint64_t>(value)};
    while (count != 0) {
        if ((count & 1) != 0) result = add_issuance(result, term);
        count >>= 1;
        if (count != 0) {
            term = { (term.high << 1) | (term.low >> 63), term.low << 1 };
        }
    }
    return result;
}
inline Amount narrow_issuance(Issuance value) {
    if (value.high != 0 || value.low > static_cast<std::uint64_t>(std::numeric_limits<Amount>::max()))
        throw std::overflow_error("issuance does not fit single amount");
    return static_cast<Amount>(value.low);
}

// Explicit parameters: no unapproved mainnet precision or height default.
struct ScheduleParameters {
    Amount initial_subsidy;
    Amount permanent_floor;
    Height halving_interval;
    Height first_reward_height;
};

class RewardSchedule {
    ScheduleParameters p_;
public:
    explicit RewardSchedule(ScheduleParameters p) : p_(p) {
        if (p.initial_subsidy <= 0 || p.permanent_floor <= 0 ||
            p.permanent_floor > p.initial_subsidy || p.halving_interval == 0 ||
            p.first_reward_height == 0)
            throw std::invalid_argument("invalid reward schedule");
    }
    Amount subsidy(Height height) const {
        if (height < p_.first_reward_height) return 0;
        const auto era = (height - p_.first_reward_height) / p_.halving_interval;
        const Amount halved = era >= 63 ? 0 : p_.initial_subsidy >> era;
        return std::max(p_.permanent_floor, halved);
    }
    // Scheduled issuance inclusive of height; not claimed/on-chain supply.
    // Supports the entire Height domain without a monetary supply cap.
    Issuance scheduled_issuance_wide(Height height) const {
        if (height < p_.first_reward_height) return {};
        Height remaining = height - p_.first_reward_height + 1;
        Issuance total{};
        Amount reward = p_.initial_subsidy;
        while (remaining != 0) {
            const Height count = reward <= p_.permanent_floor
                ? remaining : std::min(remaining, p_.halving_interval);
            total = add_issuance(total, multiply_issuance(std::max(reward, p_.permanent_floor), count));
            remaining -= count;
            reward /= 2;
        }
        return total;
    }
    // Compatibility helper for callers requiring a single transaction Amount.
    Amount scheduled_issuance(Height height) const {
        return narrow_issuance(scheduled_issuance_wide(height));
    }
};

struct RewardAllocation { Amount miner; Amount team; };
// Basis points permit explicit future per-height allocation policy by caller.
// Remainder goes to miner; this rounding policy remains a draft choice.
inline RewardAllocation allocate_reward(Amount subsidy, Amount fees, unsigned team_bps) {
    if (subsidy < 0 || fees < 0 || team_bps > 10000)
        throw std::invalid_argument("invalid allocation input");
    const Amount team = (subsidy / 10000) * team_bps
        + ((subsidy % 10000) * team_bps) / 10000;
    return {checked_add(subsidy - team, fees), team};
}

// Legacy comparison policies retained for research tests; the accepted Onuros
// ecosystem rule uses validate_ecosystem_reward, not these two-output helpers.
enum class ClaimPolicy { exact, allow_miner_underclaim };
inline bool valid_reward_claim(RewardAllocation allowed, RewardAllocation claimed,
                               ClaimPolicy policy) {
    if (allowed.miner < 0 || allowed.team < 0 || claimed.miner < 0 || claimed.team < 0)
        return false;
    if (claimed.team != allowed.team) return false;
    switch (policy) {
    case ClaimPolicy::exact: return claimed.miner == allowed.miner;
    case ClaimPolicy::allow_miner_underclaim: return claimed.miner <= allowed.miner;
    }
    return false; // Unknown policy values must never enable underclaim behavior.
}

// Arithmetic gate for already verified fees and reward amounts. The eventual
// shielded verifier must also prove recipient binding and amount commitments.
inline bool validate_block_reward(const RewardSchedule& schedule, Height height,
                                  Amount verified_fees, unsigned team_bps,
                                  RewardAllocation claimed, ClaimPolicy policy) {
    if (verified_fees < 0 || team_bps > 10000) return false;
    const Amount subsidy = schedule.subsidy(height);
    if (subsidy == 0 && verified_fees != 0) return false;
    try {
        return valid_reward_claim(allocate_reward(subsidy, verified_fees, team_bps),
                                  claimed, policy);
    } catch (const std::overflow_error&) {
        return false;
    }
}

// Accepted ecosystem policy: the miner may redirect its subsidy share only.
// The team share is unchanged; all verified fees stay with the miner.
struct EcosystemReward {
    Amount miner;
    Amount team;
    Amount ecosystem;
};
inline EcosystemReward allocate_ecosystem_reward(Amount subsidy, Amount fees,
                                                unsigned team_bps, Amount redirect) {
    if (fees < 0 || redirect < 0)
        throw std::invalid_argument("negative fee or ecosystem redirect");
    const auto base = allocate_reward(subsidy, 0, team_bps);
    if (redirect > base.miner)
        throw std::invalid_argument("ecosystem redirect exceeds miner subsidy");
    return {checked_add(base.miner - redirect, fees), base.team, redirect};
}

// Amount-only gate. Chain integration MUST also verify shielded proofs and
// enforce the configured team/ecosystem recipients. No wallet address is chosen here.
inline bool validate_ecosystem_reward(const RewardSchedule& schedule, Height height,
                                     Amount verified_fees, unsigned team_bps,
                                     EcosystemReward claimed) {
    if (claimed.miner < 0 || claimed.team < 0 || claimed.ecosystem < 0) return false;
    const auto subsidy = schedule.subsidy(height);
    if (subsidy == 0 && verified_fees != 0) return false;
    try {
        const auto expected = allocate_ecosystem_reward(subsidy, verified_fees,
                                                       team_bps, claimed.ecosystem);
        return claimed.miner == expected.miner && claimed.team == expected.team;
    } catch (const std::invalid_argument&) {
        return false;
    } catch (const std::overflow_error&) {
        return false;
    }
}

// Pure accounting transition: an invalid claim leaves caller-owned state intact.
// The caller is responsible for the correct parent state, height, verified fees,
// output proofs/recipients, and applying each accepted block exactly once.
inline std::optional<Issuance> account_ecosystem_reward(
        Issuance parent_issued, const RewardSchedule& schedule, Height height,
        Amount verified_fees, unsigned team_bps, EcosystemReward claimed) {
    if (!validate_ecosystem_reward(schedule, height, verified_fees, team_bps, claimed))
        return std::nullopt;
    try {
        return add_issuance(parent_issued,
                            {0, static_cast<std::uint64_t>(schedule.subsidy(height))});
    } catch (const std::overflow_error&) {
        return std::nullopt;
    }
}

inline bool reward_mature(Height creation, Height spending, Height maturity) {
    return spending >= creation && spending - creation >= maturity;
}
} // namespace onuros
