#include "onuros/block_production_policy.hpp"

#include <cstdlib>
#include <limits>

namespace {

using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

} // namespace

int main() {
    RollingBlockProductionBudget disabled({false, 100U, 200U, 4U});
    check(disabled.record(1U) == BlockProductionPolicyError::disabled);

    RollingBlockProductionBudget zero({true, 0U, 200U, 4U});
    check(zero.check(1U) == BlockProductionPolicyError::invalid_policy);
    RollingBlockProductionBudget reversed({true, 200U, 100U, 4U});
    check(reversed.check(1U) == BlockProductionPolicyError::invalid_policy);
    RollingBlockProductionBudget absolute_cap(
        {true, 100U, max_serialized_block_bytes + 1U, 4U});
    check(absolute_cap.check(1U) ==
          BlockProductionPolicyError::invalid_policy);

    RollingBlockProductionBudget budget({true, 100U, 200U, 4U});
    check(budget.record(100U) == BlockProductionPolicyError::none);
    check(budget.record(50U) == BlockProductionPolicyError::none);
    check(budget.record(150U) == BlockProductionPolicyError::none);
    check(budget.record(100U) == BlockProductionPolicyError::none);
    check(budget.rolling_bytes() == 400U && budget.observed_blocks() == 4U);
    check(budget.check(201U) ==
          BlockProductionPolicyError::burst_limit_exceeded);
    check(budget.check(101U) ==
          BlockProductionPolicyError::rolling_budget_exceeded);
    check(budget.record(100U) == BlockProductionPolicyError::none);
    check(budget.rolling_bytes() == 400U);

    RollingBlockProductionBudget compensated({true, 100U, 200U, 4U});
    check(compensated.record(50U) == BlockProductionPolicyError::none);
    check(compensated.record(50U) == BlockProductionPolicyError::none);
    check(compensated.record(100U) == BlockProductionPolicyError::none);
    check(compensated.record(200U) == BlockProductionPolicyError::none);
    check(compensated.rolling_bytes() == 400U);

    check(projected_archival_bytes_per_year(4U * 1024U * 1024U, 60U) ==
          2'204'526'182'400ULL);
    check(!projected_archival_bytes_per_year(0U, 60U));
    check(!projected_archival_bytes_per_year(1U, 0U));
    check(!projected_archival_bytes_per_year(
        std::numeric_limits<std::size_t>::max(), 1U));
    return 0;
}
