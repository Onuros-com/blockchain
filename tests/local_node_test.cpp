#include "onuros/local_node.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace onuros;

namespace {
unsigned checks = 0;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}

Hash256 test_pow(const BlockHeader& header) {
    return double_sha256(encode_block_header(header));
}

Block side_candidate(const Block& parent, std::uint64_t timestamp,
                     std::uint64_t body_value) {
    Block block;
    block.header.version = 1U;
    block.header.height = parent.header.height + 1U;
    block.header.previous = block_id(parent.header);
    block.header.timestamp = timestamp;
    block.header.compact_target = parent.header.compact_target;
    block.transactions = {{1U, {static_cast<std::uint8_t>(body_value)}}};
    block.header.transactions_root = transaction_root(block.transactions);
    return block;
}
}

int main() {
    const auto path = std::filesystem::temp_directory_path() /
                      "onuros-stage5-local-node-test.db";
    auto temporary = path;
    temporary += ".tmp";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(temporary, ignored);
    try {
        const auto limit = decode_compact_target(0x207fffffU);
        check(limit.has_value(), "regression-test proof-of-work limit decodes");
        LocalNodeParameters parameters;
        parameters.validation_limits = {1U, 1U, 4096U, 32U, 1024U};
        parameters.decode_limits = {4096U, 32U, 1024U};
        parameters.difficulty.target_block_seconds = 60U;
        parameters.difficulty.retarget_interval = 60U;
        parameters.difficulty.adjustment_clamp_factor = 4U;
        parameters.difficulty.proof_of_work_limit = *limit;
        parameters.max_future_seconds = 120U;
        parameters.max_database_bytes = 1U << 20U;

        LocalNode node(parameters, test_pow);
        check(node.open(path).error == LocalNodeError::none,
              "empty local node opens");
        auto genesis = node.make_candidate({{1U, {0x01U}}}, 100U);
        check(genesis.has_value() && genesis->header.height == 0U &&
              genesis->header.previous == Hash256{}, "genesis candidate prepared");
        check(node.mine(*genesis, 100U, 10'000U).error == LocalNodeError::none,
              "genesis mined, validated and committed");
        check(node.active_state().chain.size() == 1U,
              "genesis activates local chain");

        auto main = node.make_candidate({{1U, {0x02U}}}, 160U);
        check(main.has_value() && main->header.previous == block_id(genesis->header),
              "tip extension candidate prepared");
        check(node.mine(*main, 160U, 10'000U).error == LocalNodeError::none,
              "tip extension mined and committed");

        auto invalid = *node.make_candidate({{1U, {0x03U}}}, 220U);
        invalid.transactions[0].body[0] ^= 1U;
        auto result = node.submit(invalid, 220U);
        check(result.error == LocalNodeError::block_validation_failed &&
              result.validation_error == BlockValidationError::invalid_transaction_root,
              "altered transaction root rejected");

        invalid = *node.make_candidate({{1U, {0x03U}}}, 160U);
        result = node.submit(invalid, 220U);
        check(result.validation_error == BlockValidationError::timestamp_not_after_median,
              "stale timestamp rejected");

        invalid = *node.make_candidate({{1U, {0x03U}}}, 220U);
        ++invalid.header.compact_target;
        result = node.submit(invalid, 220U);
        check(result.validation_error == BlockValidationError::unexpected_target,
              "unexpected difficulty rejected");

        invalid = *node.make_candidate({{1U, {0x03U}}}, 220U);
        invalid.header.previous[0] ^= 1U;
        result = node.submit(invalid, 220U);
        check(result.error == LocalNodeError::missing_parent,
              "unknown parent rejected");

        invalid = *node.make_candidate({{1U, {0x03U}}}, 220U);
        while (hash_meets_compact_target(test_pow(invalid.header),
               invalid.header.compact_target, *limit))
            ++invalid.header.nonce;
        result = node.submit(invalid, 220U);
        check(result.validation_error == BlockValidationError::invalid_proof_of_work,
              "insufficient proof of work rejected");
        check(node.store().blocks().size() == 2U,
              "invalid blocks never reach durable storage");

        auto fork_one = side_candidate(*genesis, 161U, 0x11U);
        check(node.mine(fork_one, 221U, 10'000U).error == LocalNodeError::none,
              "valid side branch stored without activation");
        check(node.active_state().tip() == block_id(main->header),
              "equal-work fork does not replace active tip");
        auto fork_two = side_candidate(fork_one, 221U, 0x12U);
        check(node.mine(fork_two, 221U, 10'000U).error == LocalNodeError::none,
              "stronger fork mined and committed");
        check(node.active_state().tip() == block_id(fork_two.header) &&
              node.active_state().undo_log.size() == 1U,
              "stronger fork reorganizes atomically with undo");

        const PruningPolicy pruning_policy{true, 1U, 1U};
        const auto checkpoint = make_pruning_checkpoint(
            pruning_policy, block_id(genesis->header),
            *node.store().index().active_tip(), fork_two.header.shielded_root);
        check(checkpoint.accepted() &&
              node.compact_history(pruning_policy, *checkpoint.checkpoint).error ==
                  LocalNodeError::none &&
              node.store().body_availability(block_id(main->header)) ==
                  BlockBodyAvailability::archive_required,
              "validated local node compacts finalized bodies");

        LocalNode restarted(parameters, test_pow);
        check(restarted.open(path).error == LocalNodeError::none,
              "local node restarts from durable database");
        check(restarted.store().blocks().size() == 4U &&
              restarted.active_state().tip() == block_id(fork_two.header),
              "restart recovers blocks and active fork");
        const auto always_invalid_pow = [](const BlockHeader&) {
            Hash256 hash{};
            hash.fill(0xffU);
            return hash;
        };
        LocalNode rejects_unverified_database(parameters, always_invalid_pow);
        check(rejects_unverified_database.open(path).error ==
              LocalNodeError::database_error,
              "restart revalidates persisted proof of work");
        auto after_restart = restarted.make_candidate({{1U, {0x13U}}}, 281U);
        check(after_restart && after_restart->header.height == 3U &&
              after_restart->header.previous == block_id(fork_two.header),
              "mining resumes from recovered tip");

        std::cout << checks << " local-node checks passed\n";
    } catch (const std::exception& error) {
        std::filesystem::remove(path, ignored);
        std::filesystem::remove(temporary, ignored);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(temporary, ignored);
}
