#include "onuros/economics.hpp"
#include "onuros/amount.hpp"
#include "onuros/reward_policy.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <string>

using namespace onuros;
static unsigned checks = 0;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}
template<class E, class F> void rejects(F f, const char* label) {
    bool caught = false;
    try { f(); } catch (const E&) { caught = true; }
    check(caught, label);
}
int main() {
    try {
        // Confirmed eight decimals; implementation uses an unfunded genesis at height zero.
        constexpr Amount coin = atomic_units_per_coin;
        constexpr Height H = 2102400;
        RewardSchedule s({50 * coin, coin / 2, H, 1});
        constexpr std::array<Amount, 8> rewards = {
            5000000000, 2500000000, 1250000000, 625000000,
            312500000, 156250000, 78125000, 50000000};
        check(s.subsidy(0) == 0, "unfunded genesis");
        check(s.scheduled_issuance(0) == 0, "genesis supply");
        for (Height era = 0; era < rewards.size(); ++era) {
            const Height first = era * H + 1;
            check(s.subsidy(first) == rewards[era], "era first");
            check(s.subsidy(first + 1) == rewards[era], "era second");
            check(s.subsidy(first + H - 1) == rewards[era], "era last");
            if (era) check(s.subsidy(first - 1) == rewards[era - 1], "preceding era");
        }
        check(s.scheduled_issuance(7 * H) == 208597500LL * coin, "pre-floor supply");
        check(s.scheduled_issuance(7 * H + 1) == 208597500LL * coin + coin / 2, "floor entry supply");
        check(s.scheduled_issuance(8 * H) == 209648700LL * coin, "floor era supply");
        check(s.subsidy(std::numeric_limits<Height>::max()) == coin / 2, "far future floor");
        rejects<std::overflow_error>([&] { s.scheduled_issuance(std::numeric_limits<Height>::max()); }, "supply overflow");
        const auto initial = allocate_reward(50 * coin, 17, 1000);
        check(initial.miner == 45 * coin + 17 && initial.team == 5 * coin, "initial allocation plus fees");
        const auto floor = allocate_reward(coin / 2, 0, 1000);
        check(floor.miner == 45000000 && floor.team == 5000000, "floor allocation fixture");
        check(valid_reward_claim(initial, initial, ClaimPolicy::exact), "exact reward");
        check(!valid_reward_claim(initial, {initial.miner + 1, initial.team}, ClaimPolicy::exact), "excess miner");
        check(!valid_reward_claim(initial, {initial.miner - 1, initial.team + 1}, ClaimPolicy::exact), "wrong split");
        check(!valid_reward_claim(initial, {-1, initial.team}, ClaimPolicy::allow_miner_underclaim), "negative claim");
        check(!valid_reward_claim(initial, {initial.miner - 1, initial.team}, ClaimPolicy::exact), "exact underclaim");
        check(valid_reward_claim(initial, {initial.miner - 1, initial.team}, ClaimPolicy::allow_miner_underclaim), "explicit underclaim policy");
        check(!reward_mature(100, 159, 60), "immature reward");
        check(reward_mature(100, 160, 60), "mature reward");
        check(!reward_mature(100, 99, 60), "height underflow");
        check(!reward_mature(std::numeric_limits<Height>::max()-1, std::numeric_limits<Height>::max(), 60), "maturity overflow safe");
        rejects<std::invalid_argument>([] { RewardSchedule bad({50, 0, 1, 1}); }, "zero floor");
        rejects<std::invalid_argument>([] { RewardSchedule bad({50, 1, 0, 1}); }, "zero interval");
        rejects<std::invalid_argument>([] { RewardSchedule bad({50, 51, 1, 1}); }, "invalid floor");
        rejects<std::invalid_argument>([] { RewardSchedule bad({50, 1, 1, 0}); }, "funded genesis convention");
        rejects<std::invalid_argument>([] { allocate_reward(50, -1, 1000); }, "negative fees");
        rejects<std::invalid_argument>([] { allocate_reward(50, 0, 10001); }, "invalid percentage");
        rejects<std::overflow_error>([] { allocate_reward(std::numeric_limits<Amount>::max(), 1, 0); }, "fee overflow");
        const auto largest = allocate_reward(std::numeric_limits<Amount>::max(), 0, 1000);
        check(checked_add(largest.miner, largest.team) == std::numeric_limits<Amount>::max(), "large split conservation");
        RewardSchedule small({101, 3, 7, 2});
        Amount reference_sum = 0;
        Amount reference_reward = 101;
        for (Height h = 0; h < 1000; ++h) {
            if (h >= 2) {
                if (h > 2 && (h-2) % 7 == 0) reference_reward = std::max<Amount>(3, reference_reward / 2);
                reference_sum += reference_reward;
            }
            check(small.scheduled_issuance(h) == reference_sum, "independent per-block supply oracle");
        }
        // Fixed vectors generated independently with Python arbitrary-precision integers.
        struct ProductVector { Amount value; Height count; Issuance expected; };
        const ProductVector products[] = {
            {0LL, 0ULL, {0ULL, 0ULL}},
            {0LL, 18446744073709551615ULL, {0ULL, 0ULL}},
            {1LL, 18446744073709551615ULL, {0ULL, 18446744073709551615ULL}},
            {9223372036854775807LL, 18446744073709551615ULL, {9223372036854775806ULL, 9223372036854775809ULL}},
            {9149098214922407977LL, 17190733326044304849ULL, {8526150032659444008ULL, 13299122476599463545ULL}},
            {3217660691400596544LL, 5931622743903853155ULL, {1034651386879629594ULL, 10687532009272372416ULL}},
            {8115414515052700124LL, 1237246135408277288ULL, {544310974655704773ULL, 10285521529402920544ULL}},
            {8858483674108962710LL, 17003435270297954557ULL, {8165378841048400454ULL, 12266541235636735806ULL}},
            {1726003016416802578LL, 3319239580884091015ULL, {310570662547491328ULL, 1324967801710250622ULL}},
            {741467556688784652LL, 3624361811517866439ULL, {145681356352328758ULL, 9723358241366921300ULL}},
            {6848796435611020827LL, 18273421845433180152ULL, {6784446398851787858ULL, 2861273776581947176ULL}},
            {6656578632238804767LL, 12296742953770067674ULL, {4437327046178205227ULL, 6600173036666305126ULL}},
            {838638079192315872LL, 14183098242738805354ULL, {644801392579544704ULL, 7393050240625337024ULL}},
            {3536928885029885693LL, 446798319858869749ULL, {85667903071518512ULL, 10701511149882085665ULL}},
            {6832711556428873127LL, 13127483798550191393ULL, {4862446722238782418ULL, 13832356440600108423ULL}},
            {822677858577625800LL, 2963353724121272614ULL, {132158037549976915ULL, 4724060606878896560ULL}},
            {2752334063689012410LL, 9973869745751789266ULL, {1488144538588460515ULL, 17088301229608348820ULL}},
            {4947749725244588119LL, 3679047056710050788ULL, {986786827597512590ULL, 3081523424816542332ULL}},
            {5011398020040759481LL, 8748937150369487590ULL, {2376810028784976525ULL, 13653978137568526390ULL}},
            {5130543042732094657LL, 3502328190280794182ULL, {974093068034643571ULL, 9289046421571824838ULL}},
            {3052663534355873253LL, 2337480048030882554ULL, {386818404179950628ULL, 10671879288055313314ULL}},
            {3989148978259940330LL, 14986320283941065175ULL, {3240824722762997188ULL, 14512071968392151942ULL}},
            {1738272909970576990LL, 11986974872000660565ULL, {1129556175834482965ULL, 7156816012849177910ULL}},
            {1204686114495565144LL, 3109326422093451973ULL, {203058184748638049ULL, 9963292971989791928ULL}},
            {3766812140851180124LL, 14157745423572958151ULL, {2891001638852928078ULL, 17847490983938316676ULL}},
            {4898182078130060576LL, 8270421648312673706ULL, {2196053186105575998ULL, 9952736788310701888ULL}},
            {5026473920923674822LL, 3644364139916309860ULL, {993037103699300550ULL, 15533293371510156120ULL}},
            {5068798556672784340LL, 14462938269656062711ULL, {3974127918376762898ULL, 8090789728894002572ULL}},
            {3757880483571508418LL, 11644230134114809962ULL, {2372105613454518110ULL, 8166667820301494356ULL}},
            {8349622113408209705LL, 65016311332503902ULL, {29428587975468155ULL, 5266127588009980430ULL}},
            {5142223610580607226LL, 13685311017105854686ULL, {3814924148625051762ULL, 17601369015786813644ULL}},
            {6594143728049909981LL, 294087864300133813ULL, {105127367633086320ULL, 6526961267480794433ULL}},
            {4500292273572908049LL, 13421782346212980057ULL, {3274395912302207782ULL, 2470918605845903081ULL}},
            {5612688387218142221LL, 15607816048585243408ULL, {4748903521168386025ULL, 4129833463696162768ULL}},
            {3604218778728526357LL, 9388560611830566966ULL, {1834384774200749632ULL, 6101433455187517550ULL}},
            {3160408701105828111LL, 4399192016805995793ULL, {753696406921153202ULL, 3408684096184462591ULL}},
        };
        for (const auto& v : products)
            check(multiply_issuance(v.value, v.count) == v.expected, "wide product oracle");
        const auto max64 = std::numeric_limits<std::uint64_t>::max();
        check(add_issuance({0, max64}, {0, 1}) == Issuance{1, 0}, "wide carry");
        rejects<std::overflow_error>([&] { add_issuance({max64, max64}, {0, 1}); }, "wide carry overflow");
        rejects<std::overflow_error>([&] { add_issuance({max64, 0}, {1, 0}); }, "wide high overflow");
        rejects<std::invalid_argument>([] { multiply_issuance(-1, 1); }, "negative wide input");
        check(s.scheduled_issuance_wide(max64) == Issuance{50000000ULL, 20123909950000000ULL}, "full-height issuance oracle");
        RewardSchedule maximum({std::numeric_limits<Amount>::max(), std::numeric_limits<Amount>::max(), 1, 1});
        check(maximum.scheduled_issuance_wide(max64) == Issuance{9223372036854775806ULL, 9223372036854775809ULL}, "maximum schedule bound");
        check(s.scheduled_issuance_wide(0) == Issuance{}, "wide genesis");
        check(!valid_reward_claim(initial, initial, static_cast<ClaimPolicy>(255)), "unknown policy fails closed");
        check(validate_block_reward(s, 1, 17, 1000, initial, ClaimPolicy::exact), "block reward integration");
        check(!validate_block_reward(s, H+1, 17, 1000, initial, ClaimPolicy::exact), "old reward at new era");
        check(!validate_block_reward(s, 1, -1, 1000, initial, ClaimPolicy::exact), "negative verified fee");
        check(!validate_block_reward(s, 1, 0, 10001, initial, ClaimPolicy::exact), "invalid block allocation");
        check(!validate_block_reward(s, 0, 1, 1000, {1, 0}, ClaimPolicy::exact), "genesis fees rejected");
        check(validate_block_reward(s, 0, 0, 1000, {0, 0}, ClaimPolicy::exact), "zero genesis claim");
        check(!validate_block_reward(s, 1, std::numeric_limits<Amount>::max(), 0, initial, ClaimPolicy::exact), "block fee overflow rejected");
        for (Amount reward = 0; reward <= 500; ++reward) {
            for (unsigned bps = 0; bps <= 10000; bps += 1000) {
                const auto a = allocate_reward(reward, 37, bps);
                check(a.team == reward * bps / 10000, "small allocation independent formula");
                check(a.miner + a.team == reward + 37, "allocation conservation including fees");
                check(!valid_reward_claim(a, {a.miner+1, a.team}, ClaimPolicy::allow_miner_underclaim), "underclaim policy rejects excess");
            }
        }
        RewardSchedule shifts({std::numeric_limits<Amount>::max(), 1, 1, 1});
        Amount expected_reward = std::numeric_limits<Amount>::max();
        for (Height h = 1; h < 130; ++h) {
            check(shifts.subsidy(h) == expected_reward, "all shift boundaries");
            expected_reward = std::max<Amount>(1, expected_reward / 2);
        }
        const EcosystemReward redirected{44*coin, 5*coin, coin};
        check(validate_ecosystem_reward(s, 1, 0, 1000, redirected), "44 miner 5 team 1 ecosystem");
        check(validate_ecosystem_reward(s, 1, 17, 1000, {44*coin+17, 5*coin, coin}), "fees stay with miner");
        check(!validate_ecosystem_reward(s, 1, 17, 1000, {0, 5*coin, 45*coin+17}), "fees cannot move to ecosystem");
        check(validate_ecosystem_reward(s, 1, 17, 1000, {17, 5*coin, 45*coin}), "all miner subsidy redirected");
        check(validate_ecosystem_reward(s, 1, 17, 1000, {45*coin+17, 5*coin, 0}), "zero ecosystem redirect");
        check(!validate_ecosystem_reward(s, 1, 0, 1000, {44*coin, 5*coin, 0}), "missing remainder rejected");
        check(!validate_ecosystem_reward(s, 1, 0, 1000, {44*coin, 5*coin, coin+1}), "excess ecosystem coin rejected");
        check(!validate_ecosystem_reward(s, 1, 0, 1000, {44*coin, 4*coin, 2*coin}), "team diversion rejected");
        check(!validate_ecosystem_reward(s, 1, -1, 1000, redirected), "ecosystem negative fees");
        check(!validate_ecosystem_reward(s, 1, 0, 1000, {44*coin, 5*coin, -1}), "negative ecosystem claim");
        check(!validate_ecosystem_reward(s, H+1, 0, 1000, redirected), "ecosystem old era reward");
        check(validate_ecosystem_reward(s, 7*H+1, 0, 1000, {0, 5000000, 45000000}), "floor ecosystem allocation");
        check(validate_ecosystem_reward(s, 0, 0, 1000, {0, 0, 0}), "ecosystem unfunded genesis");
        check(!validate_ecosystem_reward(s, 0, 1, 1000, {1, 0, 0}), "ecosystem genesis fees rejected");
        const Issuance parent{0, 50*coin};
        const auto next = account_ecosystem_reward(parent, s, 2, 17, 1000, {44*coin+17, 5*coin, coin});
        check(next.has_value() && *next == Issuance{0, 100*coin}, "actual issuance excludes fees");
        check(parent == Issuance{0, 50*coin}, "accounting input immutable");
        check(!account_ecosystem_reward(parent, s, 2, 0, 1000, {44*coin, 5*coin, 0}), "invalid claim has no accounting result");
        check(!account_ecosystem_reward({max64, max64}, s, 1, 0, 1000, redirected), "accounting overflow rejected");
        check(validate_ecosystem_reward(s, 1, std::numeric_limits<Amount>::max(), 1000,
              {std::numeric_limits<Amount>::max(), 5*coin, 45*coin}), "separate large fee output safe");
        for (Amount subsidy = 1; subsidy <= 200; ++subsidy) {
            const Amount team = subsidy / 10;
            for (Amount donation = 0; donation <= subsidy-team; ++donation) {
                const auto a = allocate_ecosystem_reward(subsidy, 13, 1000, donation);
                check(a.team == team && a.ecosystem == donation &&
                      a.miner == subsidy-team-donation+13, "ecosystem integer allocation oracle");
                check(a.miner+a.team+a.ecosystem == subsidy+13, "three-output conservation");
            }
        }
        check(subtract_issuance({1, 0}, {0, 1}) == Issuance{0, max64}, "issuance borrow");
        check(subtract_issuance({max64, max64}, {max64, max64}) == Issuance{}, "issuance full undo");
        rejects<std::underflow_error>([] { subtract_issuance({}, {0, 1}); }, "low issuance underflow");
        rejects<std::underflow_error>([&] { subtract_issuance({0, max64}, {1, 0}); }, "high issuance underflow");
        for (const auto& v : products) {
            const auto enlarged = add_issuance(v.expected, {0, 50*coin});
            check(subtract_issuance(enlarged, {0, 50*coin}) == v.expected, "wide issuance apply undo");
        }
        check(next.has_value() && subtract_issuance(*next, {0, 50*coin}) == parent,
              "ecosystem issuance transition undo");
        struct DecimalVector { const char* text; Amount units; const char* canonical; };
        const DecimalVector decimal_vectors[] = {
            {"0", 0, "0.00000000"},
            {"0.00000001", 1, "0.00000001"},
            {"0.5", 50000000, "0.50000000"},
            {"1", 100000000, "1.00000000"},
            {"50.00000000", 5000000000LL, "50.00000000"},
            {"44", 4400000000LL, "44.00000000"},
            {"0.00000010", 10, "0.00000010"},
            {"92233720368.54775807", std::numeric_limits<Amount>::max(), "92233720368.54775807"}
        };
        for (const auto& v : decimal_vectors) {
            const auto parsed = parse_amount(v.text);
            check(parsed.has_value() && *parsed == v.units, "decimal vector exact parse");
            check(format_amount(v.units) == v.canonical, "decimal vector exact format");
        }
        const char* invalid_decimals[] = {"", ".", ".1", "1.", "01", "00.1", "-1", "+1",
            " 1", "1 ", "1e2", "1,000", "1.2.3", "0.000000001", "1.000000000",
            "92233720368.54775808", "92233720369", "999999999999999999999"};
        for (const auto* text : invalid_decimals)
            check(!parse_amount(text), "invalid decimal rejected without rounding");
        check(!parse_amount(std::string_view("1\0.0", 5)), "embedded NUL rejected");
        rejects<std::invalid_argument>([] { format_amount(-1); }, "negative format rejected");
        // Deterministic unsigned progression samples the full nonnegative Amount range.
        std::uint64_t sample = 7301;
        for (unsigned i=0; i<1000; ++i) {
            sample = sample*6364136223846793005ULL+1442695040888963407ULL;
            const auto amount = static_cast<Amount>(sample >> 1);
            const auto parsed = parse_amount(format_amount(amount));
            check(parsed.has_value() && *parsed == amount, "exact amount round trip");
        }
        const OnurosRewardPolicy policy;
        const std::array<Amount, 8> team_rewards = {
            500000000, 250000000, 125000000, 62500000,
            31250000, 15625000, 7812500, 5000000};
        for (Height era=0; era<team_rewards.size(); ++era) {
            const Height height = era*H+1;
            const auto allocation = policy.allocate(height, 17, 0);
            check(allocation.team == team_rewards[era], "permanent team share every era");
            check(policy.validate(height, 17, allocation), "fixed policy accepts allocation");
            check(!policy.validate(height, 17, {allocation.miner+1, allocation.team-1, 0}), "fixed policy rejects altered team share");
        }
        const auto forever = policy.allocate(max64, 17, 0);
        check(forever.team == 5000000 && forever.miner == 45000017, "team share never expires");
        check(policy.validate(7*H+1, 17, {17, 5000000, 45000000}), "floor full redirection preserves team");
        check(!policy.validate(7*H+1, 0, {50000000, 0, 0}), "floor cannot remove team fund");
        const auto policy_next = policy.account({}, 1, 17, {4400000017LL, 500000000, 100000000});
        check(policy_next.has_value() && *policy_next == Issuance{0, 5000000000ULL}, "fixed policy issued supply");
        check(policy.scheduled_issuance(7*H) == Issuance{0, 20859750000000000ULL}, "fixed schedule cumulative oracle");
        rejects<std::invalid_argument>([&] { policy.allocate(0, 1, 0); }, "policy no genesis fees");
        check(!policy.is_reward_mature(1, 60), "first reward immature at depth 59");
        check(policy.is_reward_mature(1, 61), "first reward spendable at depth 60");
        check(!policy.is_reward_mature(0, 1000), "genesis is not a spendable reward");
        check(!policy.is_reward_mature(100, 99), "reward cannot predate creation");
        check(policy.is_reward_mature(max64-60, max64), "maximum-height maturity boundary");
        check(!policy.is_reward_mature(max64-59, max64), "maximum-height immature boundary");
        // Miner, team, ecosystem and fee value share one origin/maturity predicate.
        for (Height created : {Height{1}, H, H+1, 7*H, 7*H+1}) {
            for (Height delta=0; delta<=61; ++delta)
                check(policy.is_reward_mature(created, created+delta) == (delta>=60),
                      "uniform maturity around reward boundaries");
        }
        check(policy.subsidy(0) == 0 && policy.subsidy(1) == 5000000000LL,
              "fixed unfunded genesis and initial reward");
        check(policy.subsidy(H) == 5000000000LL && policy.subsidy(H+1) == 2500000000LL,
              "fixed first halving indexing");
        std::cout << checks << " economic checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}
