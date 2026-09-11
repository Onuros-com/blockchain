#include "onuros/difficulty.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

using namespace onuros;

namespace {
unsigned checks = 0;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}
}

int main() {
    try {
        constexpr std::uint32_t base_compact = 0x1d00ffffU;
        const auto base = decode_compact_target(base_compact);
        check(base.has_value(), "known compact target decodes");
        check(encode_compact_target(*base) == base_compact,
              "known compact target round trips");
        check(is_canonical_compact_target(base_compact), "canonical target accepted");
        check(!decode_compact_target(0U), "zero target rejected");
        check(!decode_compact_target(0x1d80ffffU), "negative target rejected");
        check(!decode_compact_target(0x2300ffffU), "overflow target rejected");
        check(!is_canonical_compact_target(0x1d0000ffU),
              "non-canonical target rejected");

        Hash256 low_hash{};
        check(hash_meets_target(low_hash, *base), "zero hash meets target");
        Hash256 high_hash{};
        high_hash[0] = 1U;
        check(!hash_meets_target(high_hash, *base), "high hash misses target");
        check(hash_meets_compact_target(low_hash, base_compact, *base),
              "bounded compact proof of work accepted");
        check(!hash_meets_compact_target(low_hash, 0x1d01ffffU, *base),
              "target above proof-of-work limit rejected");
        const auto base_work = work_for_compact_target(base_compact, *base);
        check(base_work && !is_zero(*base_work), "compact target produces work");
        const auto easier_target = decode_compact_target(0x1d01ffffU);
        check(easier_target && work_for_target(*easier_target) < *base_work,
              "easier target contributes less accumulated work");
        Target256 maximum;
        maximum.limbs.fill(UINT32_MAX);
        Target256 one;
        one.limbs[0] = 1U;
        check(work_for_target(maximum) == one,
              "maximum target has exactly one unit of work");

        DifficultyParameters parameters;
        parameters.target_block_seconds = 60U;
        parameters.retarget_interval = 60U;
        parameters.adjustment_clamp_factor = 4U;
        parameters.proof_of_work_limit = *base;
        check(next_compact_target(base_compact, 59U, 100U, 3'640U, parameters) ==
              base_compact, "target unchanged between boundaries");
        check(next_compact_target(base_compact, 60U, 100U, 3'640U, parameters) ==
              base_compact, "on-time interval preserves target");
        check(next_compact_target(base_compact, 60U, 100U, 7'180U, parameters) ==
              base_compact, "easier target capped at proof-of-work limit");
        const auto harder = next_compact_target(base_compact, 60U, 100U, 1'870U,
                                                parameters);
        check(harder && *harder != base_compact &&
              *decode_compact_target(*harder) < *base,
              "fast interval raises difficulty");
        const auto clamped_fast = next_compact_target(base_compact, 60U, 100U, 101U,
                                                      parameters);
        const auto quarter_speed = next_compact_target(base_compact, 60U, 100U, 985U,
                                                       parameters);
        check(clamped_fast == quarter_speed, "fast adjustment clamps to four times");
        check(!next_compact_target(base_compact, 60U, 100U, 100U, parameters),
              "non-increasing interval timestamps rejected");

        const std::vector<std::uint64_t> timestamps =
            {900U, 100U, 700U, 300U, 500U, 1'100U, 200U, 800U, 600U, 400U, 1'000U};
        check(median_time_past(timestamps) == 600U, "eleven-block median calculated");
        check(median_time_past({9U, 1U, 5U}, 11U) == 5U,
              "short-chain median calculated");
        check(median_time_past({1U, 2U, 3U, 100U}, 3U) == 3U,
              "only newest window is used");
        check(!median_time_past({}, 11U), "empty median history rejected");
        check(!median_time_past({1U}, 0U), "zero median window rejected");

        std::cout << checks << " difficulty checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
