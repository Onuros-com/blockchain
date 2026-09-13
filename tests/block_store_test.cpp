#include "onuros/block_store.hpp"

#include <filesystem>
#include <fstream>
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

Block make_block(Height height, Hash256 parent, std::uint64_t timestamp,
                 std::uint64_t nonce) {
    Block block;
    block.header.height = height;
    block.header.previous = parent;
    block.header.timestamp = timestamp;
    block.header.compact_target = 0x1d00ffffU;
    block.header.nonce = nonce;
    block.transactions = {{1U, {static_cast<std::uint8_t>(height), 0x42U}}};
    block.header.transactions_root = transaction_root(block.transactions);
    return block;
}

ChainWork block_work(const Block& block) {
    return chain_work_from_target_work(
        work_for_target(*decode_compact_target(block.header.compact_target)));
}
}

int main() {
    const auto path = std::filesystem::temp_directory_path() /
                      "onuros-stage5-block-store-test.db";
    const auto cap_path = std::filesystem::temp_directory_path() /
                          "onuros-stage7-block-cap-test.db";
    auto temporary = path;
    temporary += ".tmp";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(temporary, ignored);
    std::filesystem::remove(cap_path, ignored);
    try {
        const DecodeLimits limits{4096U, 16U, 1024U};
        PersistentBlockStore store(limits, 1U << 20U);
        check(store.open(path) == BlockStoreError::none && store.blocks().empty(),
              "new database opens empty");

        const auto genesis = make_block(0U, {}, 100U, 1U);
        check(store.append(genesis, block_work(genesis)) == BlockStoreError::none,
              "genesis snapshot committed");
        const auto genesis_id = block_id(genesis.header);
        const auto main = make_block(1U, genesis_id, 160U, 2U);
        check(store.append(main, block_work(main)) == BlockStoreError::none,
              "main child committed");
        const auto size_after_main = std::filesystem::file_size(path);
        const auto fork = make_block(1U, genesis_id, 161U, 3U);
        check(store.append(fork, block_work(fork)) == BlockStoreError::none,
              "side branch committed");
        const auto size_after_fork = std::filesystem::file_size(path);
        check(size_after_fork > size_after_main && size_after_fork < size_after_main * 2U,
              "append log grows by one record instead of rewriting history");
        const auto fork_id = block_id(fork.header);
        const auto stronger = make_block(2U, fork_id, 221U, 4U);
        ChainIndexResult result;
        check(store.append(stronger, block_work(stronger), &result) == BlockStoreError::none &&
              result.reorganization.has_value(), "stronger fork committed with plan");
        check(store.index().active_tip() &&
              store.index().active_tip()->id == block_id(stronger.header),
              "strongest persisted tip selected");

        PersistentBlockStore restarted(limits, 1U << 20U);
        check(restarted.open(path) == BlockStoreError::none,
              "database restarts from snapshot");
        check(restarted.blocks().size() == 4U && restarted.index().size() == 4U,
              "all blocks and index entries recovered");
        check(restarted.index().active_tip() &&
              restarted.index().active_tip()->id == block_id(stronger.header),
              "active tip recovered exactly");
        check(restarted.find(genesis_id) != nullptr, "stored block lookup works");

        const PruningPolicy pruning_policy{true, 1U, 1U};
        const auto checkpoint = make_pruning_checkpoint(
            pruning_policy, genesis_id, *restarted.index().active_tip(),
            stronger.header.shielded_root);
        check(checkpoint.accepted(), "pruning checkpoint created");
        auto wrong_checkpoint = *checkpoint.checkpoint;
        wrong_checkpoint.active_tip.back() ^= 1U;
        const auto size_before_compaction = std::filesystem::file_size(path);
        check(restarted.compact(pruning_policy, wrong_checkpoint) ==
                  BlockStoreError::invalid_chain &&
              std::filesystem::file_size(path) == size_before_compaction,
              "mismatched checkpoint cannot rewrite database");
        check(restarted.compact(pruning_policy, *checkpoint.checkpoint) ==
                  BlockStoreError::none &&
              std::filesystem::file_size(path) < size_before_compaction,
              "validated checkpoint atomically compacts eligible bodies");
        check(restarted.body_availability(genesis_id) ==
                  BlockBodyAvailability::retained &&
              restarted.body_availability(block_id(main.header)) ==
                  BlockBodyAvailability::archive_required &&
              restarted.body_availability(fork_id) ==
                  BlockBodyAvailability::archive_required &&
              restarted.body_availability(block_id(stronger.header)) ==
                  BlockBodyAvailability::retained &&
              restarted.body_availability(Hash256{}) ==
                  BlockBodyAvailability::unknown,
              "compacted store distinguishes archive fallback from unknown data");

        PersistentBlockStore pruned_restart(limits, 1U << 20U);
        check(pruned_restart.open(path) == BlockStoreError::none &&
              pruned_restart.blocks().size() == 4U &&
              pruned_restart.body_availability(block_id(main.header)) ==
                  BlockBodyAvailability::archive_required &&
              pruned_restart.index().active_tip() &&
              pruned_restart.index().active_tip()->id ==
                  block_id(stronger.header),
              "header-only history and active chain recover after restart");
        const auto next = make_block(
            3U, block_id(stronger.header), 281U, 5U);
        check(pruned_restart.append(next, block_work(next)) ==
                  BlockStoreError::none &&
              pruned_restart.body_availability(block_id(next.header)) ==
                  BlockBodyAvailability::retained,
              "version-3 store appends new full bodies");

        {
            std::ofstream stale(temporary, std::ios::binary | std::ios::trunc);
            stale << "incomplete replacement";
        }
        PersistentBlockStore ignores_stale(limits, 1U << 20U);
        check(ignores_stale.open(path) == BlockStoreError::none &&
              ignores_stale.blocks().size() == 5U,
              "stale temporary snapshot ignored after restart");

        const auto complete_size = std::filesystem::file_size(path);
        {
            std::ofstream torn(path, std::ios::binary | std::ios::app);
            torn.write("bad", 3);
        }
        PersistentBlockStore recovers_tail(limits, 1U << 20U);
        check(recovers_tail.open(path) == BlockStoreError::none &&
              recovers_tail.blocks().size() == 5U &&
              std::filesystem::file_size(path) == complete_size,
              "incomplete append tail truncated during recovery");

        std::ifstream input(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
        check(!bytes.empty(), "snapshot has bytes");
        bytes.back() ^= 1;
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        const auto before = restarted.blocks().size();
        check(restarted.open(path) == BlockStoreError::corrupt_database,
              "checksum corruption rejected");
        check(restarted.blocks().size() == before && restarted.index().active_tip() &&
              restarted.index().active_tip()->id == block_id(stronger.header),
              "failed recovery leaves live state unchanged");

        PersistentBlockStore bounded(limits, 8U);
        check(bounded.open(path) == BlockStoreError::database_too_large,
              "oversized database rejected before allocation");

        PersistentBlockStore consensus_bounded(
            {max_serialized_block_bytes + 1U, 16U,
             static_cast<std::uint32_t>(max_serialized_block_bytes)},
            64U * 1024U * 1024U);
        check(consensus_bounded.open(cap_path) == BlockStoreError::none,
              "consensus-cap test database opens");
        auto oversized = make_block(0U, {}, 100U, 1U);
        oversized.transactions = {{1U, std::vector<std::uint8_t>(
            max_serialized_block_bytes - block_prefix_encoded_size - 8U + 1U)}};
        oversized.header.transactions_root = transaction_root(oversized.transactions);
        check(consensus_bounded.append(oversized, block_work(oversized)) ==
              BlockStoreError::invalid_block_encoding,
              "configured store cannot bypass 16 MiB consensus ceiling");

        std::cout << checks << " block-store checks passed\n";
    } catch (const std::exception& error) {
        std::filesystem::remove(path, ignored);
        std::filesystem::remove(temporary, ignored);
        std::filesystem::remove(cap_path, ignored);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(temporary, ignored);
    std::filesystem::remove(cap_path, ignored);
}
