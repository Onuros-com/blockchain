#include "onuros/compact_block_relay.hpp"

#include <iostream>
#include <map>
#include <stdexcept>

using namespace onuros;

namespace {

unsigned checks = 0U;

void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}

Block sample_block() {
    Block block;
    block.header.version = 7U;
    block.header.height = 100U;
    block.header.previous[0] = 0x11U;
    block.header.shielded_root[0] = 0x22U;
    block.header.timestamp = 1'800'000'000ULL;
    block.header.compact_target = 0x1d00ffffU;
    block.transactions = {
        {2U, {0x10U, 0x20U}},
        {2U, {0x30U, 0x40U, 0x50U}},
        {2U, {0x60U}}
    };
    block.header.transactions_root = transaction_root(block.transactions);
    return block;
}

} // namespace

int main() {
    try {
        const auto block = sample_block();
        const auto announcement = make_compact_block_announcement(block);
        check(announcement.transaction_ids.size() == block.transactions.size(),
              "announcement contains every transaction id");

        const auto encoded = encode_compact_block_announcement(announcement);
        check(encoded.size() == compact_block_prefix_encoded_size + 3U * 32U,
              "compact announcement has bounded canonical size");
        const auto decoded = decode_compact_block_announcement(
            encoded, 10U, encoded.size());
        check(decoded.has_value() &&
              encode_compact_block_announcement(*decoded) == encoded,
              "compact announcement canonical round trip");

        auto malformed = encoded;
        malformed.pop_back();
        check(!decode_compact_block_announcement(malformed, 10U, encoded.size()),
              "truncated transaction id rejected");
        malformed = encoded;
        malformed.push_back(0U);
        check(!decode_compact_block_announcement(malformed, 10U, malformed.size()),
              "trailing bytes rejected");
        check(!decode_compact_block_announcement(encoded, 2U, encoded.size()),
              "excess transaction count rejected");
        check(!decode_compact_block_announcement(encoded, 10U, encoded.size() - 1U),
              "announcement byte ceiling enforced");

        std::map<Hash256, TransactionEnvelope> pool;
        pool.emplace(transaction_id(block.transactions[0]), block.transactions[0]);
        pool.emplace(transaction_id(block.transactions[2]), block.transactions[2]);
        auto result = reconstruct_compact_block(announcement, pool);
        check(result.error == CompactBlockReconstructionError::missing_transactions &&
              result.missing_indexes == std::vector<std::uint32_t>{1U},
              "receiver requests only missing transactions");

        pool.emplace(transaction_id(block.transactions[1]), block.transactions[1]);
        result = reconstruct_compact_block(announcement, pool);
        check(result.complete() && encode_block(result.block) == encode_block(block),
              "mempool transactions reconstruct the exact full block");

        auto wrong_root = announcement;
        wrong_root.header.transactions_root[0] ^= 1U;
        result = reconstruct_compact_block(wrong_root, pool);
        check(result.error ==
                  CompactBlockReconstructionError::transaction_root_mismatch,
              "incorrect transaction root rejected after reconstruction");

        std::cout << checks << " compact-block-relay checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
