#pragma once

#include "onuros/amount.hpp"

namespace onuros {
// Confirmed economics. No per-call subsidy, halving or team-rate override.
inline constexpr Amount onuros_initial_subsidy = 50 * atomic_units_per_coin;
inline constexpr Amount onuros_permanent_floor = atomic_units_per_coin / 2;
inline constexpr Height onuros_halving_interval = 2102400;
inline constexpr unsigned onuros_team_basis_points = 1000;
inline constexpr Height onuros_reward_maturity = 60;
// Implementation indexing: unfunded genesis is 0; first reward-bearing block is 1.
inline constexpr Height onuros_first_reward_height = 1;

class OnurosRewardPolicy {
    const RewardSchedule schedule_;
public:
    OnurosRewardPolicy()
        : schedule_({onuros_initial_subsidy, onuros_permanent_floor,
                     onuros_halving_interval, onuros_first_reward_height}) {}

    // Apply uniformly to every verified reward output, including collected fees.
    // Source height/reward origin must be authenticated by state/proof validation.
    // This method defines no ordinary-payment spend lock.
    bool is_reward_mature(Height creation_height, Height spending_height) const {
        return creation_height >= onuros_first_reward_height &&
               reward_mature(creation_height, spending_height, onuros_reward_maturity);
    }

    Amount subsidy(Height height) const { return schedule_.subsidy(height); }
    Issuance scheduled_issuance(Height height) const {
        return schedule_.scheduled_issuance_wide(height);
    }
    EcosystemReward allocate(Height height, Amount verified_fees, Amount redirect) const {
        if (subsidy(height) == 0 && verified_fees != 0)
            throw std::invalid_argument("fees before first reward block");
        return allocate_ecosystem_reward(subsidy(height), verified_fees,
                                         onuros_team_basis_points, redirect);
    }
    bool validate(Height height, Amount verified_fees, EcosystemReward claimed) const {
        return validate_ecosystem_reward(schedule_, height, verified_fees,
                                         onuros_team_basis_points, claimed);
    }
    std::optional<Issuance> account(Issuance parent_issued, Height height,
                                    Amount verified_fees, EcosystemReward claimed) const {
        return account_ecosystem_reward(parent_issued, schedule_, height,
                                        verified_fees, onuros_team_basis_points, claimed);
    }
};
} // namespace onuros
