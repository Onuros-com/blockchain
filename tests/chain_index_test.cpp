#include "onuros/chain_index.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

using namespace onuros;

namespace {

unsigned checks = 0;

void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}

Hash256 id(std::uint8_t value) {
    Hash256 result{};
    result[0] = value;
    return result;
}

} // namespace

int main() {
    try {
        check(add_chain_work(chain_work(7), chain_work(9)) == chain_work(16),
              "chain work addition");
        ChainWork carry_left;
        carry_left.limbs[0] = std::numeric_limits<std::uint64_t>::max();
        ChainWork carry_expected;
        carry_expected.limbs[1] = 1U;
        check(add_chain_work(carry_left, chain_work(1)) == carry_expected,
              "chain work limb carry");
        ChainWork maximum;
        maximum.limbs.fill(std::numeric_limits<std::uint64_t>::max());
        check(!add_chain_work(maximum, chain_work(1)), "chain work overflow rejected");

        ChainIndex empty;
        check(empty.add_block(id(2), id(1), 1, chain_work(1), 101).error ==
              ChainIndexError::missing_genesis, "block before genesis rejected");
        check(empty.add_genesis(id(1), {}, 100).error == ChainIndexError::zero_block_work,
              "zero-work genesis rejected");
        check(empty.add_genesis(id(1), chain_work(1), 100).error == ChainIndexError::none,
              "genesis accepted");
        check(empty.add_genesis(id(9), chain_work(1), 100).error ==
              ChainIndexError::genesis_already_exists, "second genesis rejected");

        const auto first = empty.add_block(id(2), id(1), 1, chain_work(2), 160);
        check(first.error == ChainIndexError::none && first.reorganization.has_value() &&
              first.reorganization->disconnect.empty() &&
              first.reorganization->connect == std::vector<Hash256>{id(2)},
              "first child activates");
        check(empty.active_tip() && empty.active_tip()->id == id(2) &&
              empty.active_tip()->accumulated_work == chain_work(3),
              "active tip tracks accumulated work");
        check(empty.add_block(id(2), id(1), 1, chain_work(2), 160).error ==
              ChainIndexError::duplicate_block, "duplicate block rejected");
        check(empty.add_block(id(3), id(99), 2, chain_work(2), 220).error ==
              ChainIndexError::unknown_parent, "unknown parent rejected");
        check(empty.add_block(id(3), id(2), 7, chain_work(2), 220).error ==
              ChainIndexError::invalid_height, "nonconsecutive height rejected");
        check(empty.add_block(id(3), id(2), 2, {}, 220).error ==
              ChainIndexError::zero_block_work, "zero block work rejected");

        const auto weak_fork = empty.add_block(id(4), id(1), 1, chain_work(1), 161);
        check(weak_fork.error == ChainIndexError::none && !weak_fork.reorganization &&
              empty.active_tip()->id == id(2), "weaker fork remains inactive");
        const auto strong_fork = empty.add_block(id(5), id(4), 2, chain_work(5), 221);
        check(strong_fork.error == ChainIndexError::none &&
              strong_fork.reorganization.has_value() &&
              strong_fork.reorganization->disconnect == std::vector<Hash256>{id(2)} &&
              strong_fork.reorganization->connect ==
                  (std::vector<Hash256>{id(4), id(5)}),
              "stronger fork produces ordered reorganization");
        check(empty.active_tip()->id == id(5) && empty.size() == 4U,
              "stronger accumulated-work tip activates");

        ChainIndex overflow;
        check(overflow.add_genesis(id(10), maximum, 1).error == ChainIndexError::none,
              "maximum-work genesis represented");
        check(overflow.add_block(id(11), id(10), 1, chain_work(1), 2).error ==
              ChainIndexError::accumulated_work_overflow,
              "overflowing descendant rejected atomically");
        check(overflow.size() == 1U && overflow.active_tip()->id == id(10),
              "overflow rejection leaves index unchanged");

        std::cout << checks << " chain-index checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
