#include "onuros/pruning.hpp"

#include <cstdlib>
#include <vector>

namespace {

using namespace onuros;

void check(bool condition) {
    if (!condition) std::abort();
}

Hash256 value(std::uint8_t byte) {
    Hash256 hash{};
    hash.back() = byte;
    return hash;
}

} // namespace

int main() {
    const auto genesis = value(1U);
    const auto root = value(2U);
    const ChainEntry tip{
        value(3U), value(4U), 100U, chain_work(5U), chain_work(1'000U), 6U};

    PruningPolicy disabled;
    check(make_pruning_checkpoint(disabled, genesis, tip, root).error ==
          PruningCheckpointError::disabled);

    PruningPolicy unsafe{true, 30U, 0U};
    check(make_pruning_checkpoint(unsafe, genesis, tip, root).error ==
          PruningCheckpointError::unsafe_policy);

    const PruningPolicy policy{true, 20U, 30U};
    const auto result = make_pruning_checkpoint(policy, genesis, tip, root);
    check(result.accepted());
    const auto& checkpoint = *result.checkpoint;
    check(checkpoint.tip_height == 100U);
    check(checkpoint.prune_below_height == 71U);
    check(!checkpoint.body_may_be_pruned(0U));
    check(checkpoint.body_may_be_pruned(1U));
    check(checkpoint.body_may_be_pruned(70U));
    check(!checkpoint.body_may_be_pruned(71U));
    check(!checkpoint.body_may_be_pruned(100U));
    check(validate_pruning_checkpoint(
              checkpoint, policy, genesis, tip, root) ==
          PruningCheckpointError::none);

    const auto encoded = encode_pruning_checkpoint(checkpoint);
    check(encoded.size() == pruning_detail::checkpoint_encoded_size);
    const auto decoded = decode_pruning_checkpoint(encoded);
    check(decoded.has_value());
    check(decoded->active_tip == checkpoint.active_tip);
    check(decoded->accumulated_work == checkpoint.accumulated_work);
    check(decoded->prune_below_height == checkpoint.prune_below_height);

    for (std::size_t offset = 0U; offset < encoded.size(); ++offset) {
        auto corrupted = encoded;
        corrupted[offset] ^= 0x80U;
        check(!decode_pruning_checkpoint(corrupted));
    }
    auto truncated = encoded;
    truncated.pop_back();
    check(!decode_pruning_checkpoint(truncated));
    auto trailing = encoded;
    trailing.push_back(0U);
    check(!decode_pruning_checkpoint(trailing));

    auto changed = checkpoint;
    ++changed.version;
    check(validate_pruning_checkpoint(changed, policy, genesis, tip, root) ==
          PruningCheckpointError::invalid_version);
    changed = checkpoint;
    changed.genesis = value(9U);
    check(validate_pruning_checkpoint(changed, policy, genesis, tip, root) ==
          PruningCheckpointError::wrong_genesis);
    changed = checkpoint;
    changed.active_tip = value(9U);
    check(validate_pruning_checkpoint(changed, policy, genesis, tip, root) ==
          PruningCheckpointError::tip_mismatch);
    changed = checkpoint;
    changed.shielded_root = value(9U);
    check(validate_pruning_checkpoint(changed, policy, genesis, tip, root) ==
          PruningCheckpointError::shielded_root_mismatch);
    changed = checkpoint;
    changed.accumulated_work = chain_work(999U);
    check(validate_pruning_checkpoint(changed, policy, genesis, tip, root) ==
          PruningCheckpointError::accumulated_work_mismatch);
    changed = checkpoint;
    ++changed.prune_below_height;
    check(validate_pruning_checkpoint(changed, policy, genesis, tip, root) ==
          PruningCheckpointError::horizon_mismatch);

    const PruningPolicy changed_policy{true, 21U, 30U};
    check(validate_pruning_checkpoint(
              checkpoint, changed_policy, genesis, tip, root) ==
          PruningCheckpointError::policy_mismatch);
    auto rolled_back_tip = tip;
    --rolled_back_tip.height;
    check(validate_pruning_checkpoint(
              checkpoint, policy, genesis, rolled_back_tip, root) ==
          PruningCheckpointError::tip_mismatch);

    const ChainEntry shallow{
        value(7U), genesis, 10U, chain_work(5U), chain_work(50U), 8U};
    const auto shallow_checkpoint =
        make_pruning_checkpoint(policy, genesis, shallow, root);
    check(shallow_checkpoint.accepted());
    check(shallow_checkpoint.checkpoint->prune_below_height == 0U);
    check(!shallow_checkpoint.checkpoint->body_may_be_pruned(1U));
    return 0;
}
