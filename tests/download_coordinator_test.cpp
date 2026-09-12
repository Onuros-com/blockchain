#include "onuros/download_coordinator.hpp"

#include <iostream>
#include <filesystem>
#include <stdexcept>

using namespace onuros;

namespace {
unsigned checks = 0U;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}
Block block() {
    Block result;
    result.header.version = 1U;
    result.header.height = 1U;
    for (std::uint8_t i = 0U; i < 12U; ++i)
        result.transactions.push_back({1U, std::vector<std::uint8_t>(64U, i)});
    result.header.transactions_root = transaction_root(result.transactions);
    return result;
}
}

int main() {
    try {
        const auto sample = block();
        const auto identifier = block_id(sample.header);
        BlockDownloadCoordinator coordinator(2U, 30U);
        check(coordinator.claim(identifier, 1U, 100U) ==
                  DownloadClaimResult::claimed,
              "first peer owns block download");
        check(coordinator.claim(identifier, 2U, 100U) ==
                  DownloadClaimResult::owned_by_other_peer &&
              !coordinator.accepts_from(identifier, 2U, 100U),
              "second peer cannot duplicate active download");
        coordinator.record_avoided(16U * 1024U * 1024U);
        coordinator.release_peer(1U);
        check(coordinator.claim(identifier, 2U, 101U) ==
                  DownloadClaimResult::claimed,
              "fallback peer takes failed lease");

        const std::vector<Hash256> transaction_ids{
            transaction_id(sample.transactions[0]),
            transaction_id(sample.transactions[1])};
        check(coordinator.reserve_transactions(transaction_ids).size() == 2U &&
              coordinator.reserve_transactions(transaction_ids).empty(),
              "cross-peer transaction requests deduplicated");
        coordinator.finish_transaction(transaction_ids[0]);
        check(coordinator.reserve_transactions({transaction_ids[0]}).size() == 1U,
              "completed transaction reservation can be retried safely");

        std::vector<std::uint32_t> indexes;
        for (std::uint32_t i = 0U; i < sample.transactions.size(); ++i)
            indexes.push_back(i);
        const auto chunks = make_block_transaction_chunks(sample, indexes, 260U);
        check(chunks && chunks->size() > 3U,
              "test response is split into resumable chunks");
        BlockChunkLimits limits;
        limits.maximum_chunk_bytes = 260U;
        limits.maximum_transactions_per_chunk = 4U;
        limits.maximum_transaction_body_bytes = 128U;
        limits.maximum_total_chunks = 32U;
        limits.maximum_total_transactions = 32U;
        limits.maximum_total_transfer_bytes = 4096U;

        ResumableBlockTransactionAssembler partial(identifier, limits);
        check(partial.add((*chunks)[2]) == BlockChunkAssemblyError::none &&
              partial.add((*chunks)[0]) == BlockChunkAssemblyError::none,
              "out-of-order chunks accepted for interruption recovery");
        const auto checkpoint = encode_resume_checkpoint(partial);
        ResumableBlockTransactionAssembler resumed(identifier, limits);
        check(restore_resume_checkpoint(checkpoint, resumed, limits) &&
              resumed.received_bytes() == partial.received_bytes(),
              "received chunks survive checkpoint restoration");
        const auto checkpoint_path = std::filesystem::temp_directory_path() /
            "onuros-stage7-resume.checkpoint";
        std::error_code ignored;
        std::filesystem::remove(checkpoint_path, ignored);
        check(save_resume_checkpoint(checkpoint_path, partial),
              "resume checkpoint is atomically persisted");
        ResumableBlockTransactionAssembler disk_resumed(identifier, limits);
        check(load_resume_checkpoint(checkpoint_path, disk_resumed, limits) &&
              disk_resumed.received_bytes() == partial.received_bytes(),
              "persisted checkpoint restores after restart");
        std::filesystem::remove(checkpoint_path, ignored);
        const auto missing = resumed.missing_sequences();
        check(missing.size() == chunks->size() - 2U,
              "resume requests only missing chunk sequences");
        for (const auto sequence : missing)
            check(resumed.add((*chunks)[sequence]) == BlockChunkAssemblyError::none,
                  "missing chunk accepted after reconnect");
        check(resumed.complete() &&
              resumed.transactions().size() == sample.transactions.size(),
              "resumed transfer completes exact transaction set");
        check(resumed.add((*chunks)[0]) == BlockChunkAssemblyError::none,
              "identical duplicate chunk is idempotent");
        auto mutation = (*chunks)[0];
        mutation.transactions[0].transaction.body[0] ^= 1U;
        check(resumed.add(mutation) ==
                  BlockChunkAssemblyError::inconsistent_manifest,
              "conflicting duplicate chunk rejected");

        coordinator.record_useful(resumed.received_bytes());
        coordinator.record_resumed(partial.received_bytes());
        coordinator.complete(identifier);
        check(coordinator.accounting().avoided_duplicate_bytes ==
                  16U * 1024U * 1024U &&
              coordinator.accounting().resumed_bytes == partial.received_bytes() &&
              coordinator.active_downloads() == 0U,
              "bandwidth savings and completion are accounted");

        std::cout << checks << " download-coordinator checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
