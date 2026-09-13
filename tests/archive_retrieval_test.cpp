#include "onuros/block_transfer.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace onuros;

namespace {

unsigned checks = 0U;

void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}

Block make_block(Height height, Hash256 parent, std::uint64_t nonce,
                 std::uint8_t marker) {
    Block block;
    block.header.height = height;
    block.header.previous = parent;
    block.header.timestamp = 100U + height * 60U;
    block.header.compact_target = 0x1d00ffffU;
    block.header.nonce = nonce;
    block.header.shielded_root.back() = marker;
    block.transactions = {{1U, std::vector<std::uint8_t>(320U, marker)}};
    block.header.transactions_root = transaction_root(block.transactions);
    return block;
}

ChainWork work(const Block& block) {
    return chain_work_from_target_work(
        work_for_target(*decode_compact_target(block.header.compact_target)));
}

void append(PersistentBlockStore& first, PersistentBlockStore& second,
            const Block& block) {
    check(first.append(block, work(block)) == BlockStoreError::none,
          "archive append succeeds");
    check(second.append(block, work(block)) == BlockStoreError::none,
          "pruned append succeeds");
}

} // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path();
    const auto archive_path = directory / "onuros-archive-retrieval-full.db";
    const auto pruned_path = directory / "onuros-archive-retrieval-pruned.db";
    std::error_code ignored;
    for (const auto& path : {archive_path, pruned_path}) {
        std::filesystem::remove(path, ignored);
        std::filesystem::remove(path.string() + ".tmp", ignored);
    }

    try {
        const DecodeLimits decode_limits{4096U, 16U, 1024U};
        constexpr std::size_t database_limit = 1U << 20U;
        PersistentBlockStore archive(decode_limits, database_limit);
        PersistentBlockStore pruned(decode_limits, database_limit);
        check(archive.open(archive_path) == BlockStoreError::none,
              "archive database opens");
        check(pruned.open(pruned_path) == BlockStoreError::none,
              "pruned database opens");

        std::vector<Block> main_chain;
        main_chain.push_back(make_block(0U, {}, 1U, 10U));
        append(archive, pruned, main_chain.back());
        for (Height height = 1U; height <= 6U; ++height) {
            main_chain.push_back(make_block(
                height, block_id(main_chain.back().header), height + 1U,
                static_cast<std::uint8_t>(10U + height)));
            append(archive, pruned, main_chain.back());
        }

        const PruningPolicy policy{true, 2U, 2U};
        const auto* initial_tip = pruned.index().active_tip();
        check(initial_tip != nullptr, "initial active tip exists");
        const auto checkpoint = make_pruning_checkpoint(
            policy, block_id(main_chain.front().header), *initial_tip,
            main_chain.back().header.shielded_root);
        check(checkpoint.accepted() &&
                  pruned.compact(policy, *checkpoint.checkpoint) ==
                      BlockStoreError::none,
              "pruned database compacts at chain-bound horizon");
        const auto full_bytes = std::filesystem::file_size(archive_path);
        const auto pruned_bytes = std::filesystem::file_size(pruned_path);
        check(pruned_bytes < full_bytes,
              "pruned database uses less disk than archive database");

        const auto requested_id = block_id(main_chain[3U].header);
        check(pruned.body_availability(requested_id) ==
                  BlockBodyAvailability::archive_required,
              "historical body explicitly requires archive");
        const auto response = serve_archive_block(
            archive, {requested_id}, 256U);
        check(response.available() && response.chunks.size() > 1U,
              "archive peer produces bounded response chunks");
        check(serve_archive_block(pruned, {requested_id}, 256U).error ==
                  ArchiveBlockServeError::body_unavailable,
              "pruned peer cannot claim archive availability");

        ArchiveBlockLimits transfer_limits;
        transfer_limits.maximum_chunk_bytes = 256U;
        transfer_limits.maximum_total_chunks = 32U;
        transfer_limits.maximum_block_bytes = 4096U;
        ArchiveBlockAssembler assembler(
            pruned.find(requested_id)->block.header, decode_limits,
            transfer_limits);
        ArchiveBlockAssemblyResult assembled;
        for (const auto& chunk : response.chunks) {
            const auto wire = encode_archive_block_chunk(chunk);
            const auto decoded = decode_archive_block_chunk(
                wire, transfer_limits);
            check(decoded.has_value(), "archive wire chunk decodes");
            assembled = assembler.add(*decoded);
            check(assembled.error == ArchiveBlockAssemblyError::none,
                  "archive wire chunk assembles");
        }
        check(assembled.block &&
                  pruned.restore_body(*assembled.block) == BlockStoreError::none,
              "verified archive body restores atomically");
        check(pruned.body_availability(requested_id) ==
                  BlockBodyAvailability::retained &&
              std::filesystem::file_size(pruned_path) < full_bytes,
              "single restored body preserves disk reduction");

        PersistentBlockStore archive_restart(decode_limits, database_limit);
        PersistentBlockStore pruned_restart(decode_limits, database_limit);
        check(archive_restart.open(archive_path) == BlockStoreError::none &&
                  pruned_restart.open(pruned_path) == BlockStoreError::none,
              "archive and pruned databases restart");
        check(archive_restart.index().active_tip()->id ==
                  pruned_restart.index().active_tip()->id &&
              encode_block(archive_restart.find(requested_id)->block) ==
                  encode_block(pruned_restart.find(requested_id)->block),
              "restart preserves converged tip and restored body");

        const auto fork_six = make_block(
            6U, block_id(main_chain[5U].header), 100U, 70U);
        append(archive_restart, pruned_restart, fork_six);
        const auto fork_seven = make_block(
            7U, block_id(fork_six.header), 101U, 71U);
        append(archive_restart, pruned_restart, fork_seven);
        check(archive_restart.index().active_tip()->id ==
                  block_id(fork_seven.header) &&
              pruned_restart.index().active_tip()->id ==
                  block_id(fork_seven.header),
              "permitted stronger-branch reorganization converges");

        PersistentBlockStore final_archive(decode_limits, database_limit);
        PersistentBlockStore final_pruned(decode_limits, database_limit);
        check(final_archive.open(archive_path) == BlockStoreError::none &&
                  final_pruned.open(pruned_path) == BlockStoreError::none &&
              final_archive.index().active_tip()->id ==
                  final_pruned.index().active_tip()->id,
              "reorganized archive and pruned tips survive restart");
        check(final_archive.find(block_id(fork_seven.header))->
                      block.header.shielded_root ==
                  final_pruned.find(block_id(fork_seven.header))->
                      block.header.shielded_root,
              "reorganized shielded-root commitments converge");

        std::cout << checks << " archive retrieval checks passed"
                  << " full_bytes=" << full_bytes
                  << " pruned_bytes=" << pruned_bytes << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        for (const auto& path : {archive_path, pruned_path}) {
            std::filesystem::remove(path, ignored);
            std::filesystem::remove(path.string() + ".tmp", ignored);
        }
        return 1;
    }

    for (const auto& path : {archive_path, pruned_path}) {
        std::filesystem::remove(path, ignored);
        std::filesystem::remove(path.string() + ".tmp", ignored);
    }
    return 0;
}
