#include "onuros/block_validation.hpp"

#include <iostream>
#include <stdexcept>

using namespace onuros;

namespace {

unsigned checks = 0;

void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}

Block valid_block() {
    Block block;
    block.header.version = 1;
    block.header.height = 8;
    block.header.previous[0] = 0x11U;
    block.header.timestamp = 1'000U;
    block.header.compact_target = 0x1d00ffffU;
    block.header.nonce = 9U;
    block.header.mix_hash[0] = 0x22U;
    block.transactions = {{1, {1U, 2U}}, {1, {3U, 4U, 5U}}};
    block.header.transactions_root = transaction_root(block.transactions);
    return block;
}

} // namespace

int main() {
    try {
        const BlockValidationLimits limits{1U, 1U, 1024U, 8U, 128U};
        BlockContext context{};
        context.expected_height = 8;
        context.expected_parent[0] = 0x11U;
        context.median_time_past = 999U;
        context.adjusted_time = 1'001U;
        context.max_future_seconds = 120U;
        context.expected_compact_target = 0x1d00ffffU;
        const auto accepts_work = [](const BlockHeader&) { return true; };
        auto block = valid_block();
        check(validate_block(block, limits, context, accepts_work) ==
              BlockValidationError::none, "valid contextual block accepted");

        auto changed = block;
        ++changed.header.version;
        check(validate_block(changed, limits, context, accepts_work) ==
              BlockValidationError::unsupported_block_version, "block version rejected");
        changed = block;
        ++changed.transactions[0].version;
        changed.header.transactions_root = transaction_root(changed.transactions);
        check(validate_block(changed, limits, context, accepts_work) ==
              BlockValidationError::unsupported_transaction_version,
              "transaction version rejected");
        check(validate_block(block, {1U, 1U, 1024U, 1U, 128U}, context, accepts_work) ==
              BlockValidationError::too_many_transactions, "transaction count rejected");
        check(validate_block(block, {1U, 1U, 1024U, 8U, 1U}, context, accepts_work) ==
              BlockValidationError::transaction_too_large, "transaction size rejected");
        check(validate_block(block, {1U, 1U, 140U, 8U, 128U}, context, accepts_work) ==
              BlockValidationError::block_too_large, "block size rejected");

        changed = block;
        changed.transactions = {{1U, std::vector<std::uint8_t>(
            max_serialized_block_bytes - block_prefix_encoded_size - 8U + 1U)}};
        check(validate_block(changed,
              {1U, 1U, max_serialized_block_bytes + 1U, 8U,
               static_cast<std::uint32_t>(max_serialized_block_bytes)},
              context, accepts_work) == BlockValidationError::block_too_large,
              "configured validator cannot bypass 16 MiB consensus ceiling");

        changed = block;
        changed.transactions[0].body[0] ^= 1U;
        check(validate_block(changed, limits, context, accepts_work) ==
              BlockValidationError::invalid_transaction_root, "wrong Merkle root rejected");
        changed = block;
        ++changed.header.height;
        check(validate_block(changed, limits, context, accepts_work) ==
              BlockValidationError::invalid_height, "wrong height rejected");
        changed = block;
        changed.header.previous[0] ^= 1U;
        check(validate_block(changed, limits, context, accepts_work) ==
              BlockValidationError::invalid_parent, "wrong parent rejected");
        changed = block;
        changed.header.timestamp = context.median_time_past;
        check(validate_block(changed, limits, context, accepts_work) ==
              BlockValidationError::timestamp_not_after_median,
              "old timestamp rejected");
        changed = block;
        changed.header.timestamp = context.adjusted_time + context.max_future_seconds + 1U;
        check(validate_block(changed, limits, context, accepts_work) ==
              BlockValidationError::timestamp_too_far_in_future,
              "future timestamp rejected");
        changed = block;
        ++changed.header.compact_target;
        check(validate_block(changed, limits, context, accepts_work) ==
              BlockValidationError::unexpected_target, "wrong target rejected");
        const auto rejects_work = [](const BlockHeader&) { return false; };
        check(validate_block(block, limits, context, rejects_work) ==
              BlockValidationError::invalid_proof_of_work, "invalid work rejected");

        std::cout << checks << " block-validation checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
