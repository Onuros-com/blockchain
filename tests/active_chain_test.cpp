#include "onuros/active_chain.hpp"

#include <iostream>
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
}

int main() {
    try {
        ChainIndex index;
        check(index.add_genesis(id(1U), chain_work(1U), 100U).error ==
              ChainIndexError::none, "genesis indexed");
        auto state = rebuild_active_chain(index);
        check(state && state->chain == std::vector<Hash256>{id(1U)},
              "genesis active state rebuilt");

        const auto first = index.add_block(id(2U), id(1U), 1U,
                                           chain_work(3U), 160U);
        check(first.reorganization.has_value(), "extension plan created");
        check(apply_reorganization_atomically(*state, *first.reorganization, index) ==
              ReorganizationError::none && state->tip() == id(2U),
              "extension applied atomically");

        check(index.add_block(id(3U), id(1U), 1U, chain_work(1U), 161U).error ==
              ChainIndexError::none, "side branch indexed");
        const auto stronger = index.add_block(id(4U), id(3U), 2U,
                                              chain_work(5U), 221U);
        check(stronger.reorganization.has_value(), "strong-fork plan created");
        check(apply_reorganization_atomically(*state, *stronger.reorganization, index) ==
              ReorganizationError::none, "strong-fork plan applied");
        check(state->chain == (std::vector<Hash256>{id(1U), id(3U), id(4U)}),
              "disconnect and connects ordered correctly");
        check(state->undo_log.size() == 1U &&
              state->undo_log.back().disconnected == id(2U) &&
              state->undo_log.back().restored_parent == id(1U),
              "disconnect undo recorded");

        const auto unchanged = *state;
        ReorganizationPlan bad_disconnect{{id(3U)}, {}};
        check(apply_reorganization_atomically(*state, bad_disconnect, index) ==
              ReorganizationError::disconnect_order_mismatch,
              "out-of-order disconnect rejected");
        check(state->chain == unchanged.chain && state->undo_log == unchanged.undo_log,
              "failed disconnect leaves state unchanged");

        ReorganizationPlan unknown{{}, {id(99U)}};
        check(apply_reorganization_atomically(*state, unknown, index) ==
              ReorganizationError::missing_entry,
              "unknown connect rejected");
        check(state->chain == unchanged.chain, "failed connect leaves state unchanged");

        const auto rebuilt = rebuild_active_chain(index);
        check(rebuilt && rebuilt->chain == state->chain,
              "active state reconstructs after restart");

        std::cout << checks << " active-chain checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
