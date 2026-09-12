#include "onuros/mini_node_history.hpp"

#include <iostream>
#include <stdexcept>

using namespace onuros;

namespace {
unsigned checks = 0U;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}
Block next_block(const std::optional<BlockHeader>& parent, Height height) {
    Block block;
    block.header.version = 7U;
    block.header.height = height;
    if (parent) block.header.previous = block_id(*parent);
    block.header.timestamp = 100U + height * 60U;
    block.transactions = {{2U, std::vector<std::uint8_t>(1000U,
        static_cast<std::uint8_t>(height))}};
    block.header.transactions_root = transaction_root(block.transactions);
    block.header.shielded_root.back() = static_cast<std::uint8_t>(height);
    return block;
}
}

int main() {
    try {
        MiniNodePolicy policy;
        policy.retained_body_depth = 3U;
        policy.checkpoint_interval = 5U;
        policy.maximum_headers = 20U;
        policy.maximum_retained_body_bytes = 1U << 20U;
        MiniNodeHistory history(policy);
        std::optional<BlockHeader> parent;
        std::size_t unpruned_bytes = 0U;
        for (Height height = 0U; height < 10U; ++height) {
            auto block = next_block(parent, height);
            unpruned_bytes += encode_block(block).size();
            check(history.record_after_validation(block) ==
                      MiniNodeHistoryError::none,
                  "validated active block recorded");
            parent = block.header;
        }
        check(history.headers().size() == 10U,
              "mini node retains complete header chain");
        check(history.retained_bodies().size() == 3U &&
              history.has_body(7U) && history.has_body(9U) &&
              !history.has_body(6U),
              "mini node retains only reorganization window bodies");
        check(history.retained_body_bytes() < unpruned_bytes,
              "pruning reduces retained block-body bytes");
        check(history.can_reorganize_from(6U) &&
              !history.can_reorganize_from(5U),
              "deep reorganization requires full-peer recovery");
        check(history.checkpoints().size() == 2U &&
              history.checkpoints().back().height == 5U,
              "periodic header/state checkpoints recorded");

        auto disconnected = next_block(std::nullopt, 10U);
        check(history.record_after_validation(disconnected) ==
                  MiniNodeHistoryError::non_contiguous,
              "disconnected history rejected");
        auto invalid = next_block(parent, 10U);
        invalid.transactions.front().body.front() ^= 1U;
        check(history.record_after_validation(invalid) ==
                  MiniNodeHistoryError::invalid_transaction_root,
              "invalid transaction root rejected before retention");

        MiniNodeHistory invalid_policy({0U, 5U, 20U, 1024U});
        check(invalid_policy.record_after_validation(next_block(std::nullopt, 0U)) ==
                  MiniNodeHistoryError::invalid_policy,
              "unsafe zero reorganization window rejected");

        std::cout << checks << " mini-node history checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
