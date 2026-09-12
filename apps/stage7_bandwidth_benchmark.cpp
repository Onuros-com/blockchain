#include "onuros/block_validation.hpp"
#include "onuros/stage7_relay.hpp"

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace onuros;

int main() {
    try {
        constexpr std::size_t transaction_count = 1800U;
        constexpr std::size_t transaction_body_bytes = 9165U;
        constexpr std::size_t chunk_bytes = 256U * 1024U;
        Block block;
        block.header.version = 7U;
        block.header.height = 1U;
        block.header.timestamp = 1'800'000'000ULL;
        block.header.compact_target = 0x207fffffU;
        block.transactions.reserve(transaction_count);
        for (std::size_t i = 0U; i < transaction_count; ++i) {
            std::vector<std::uint8_t> body(transaction_body_bytes, 0U);
            for (std::size_t byte = 0U; byte < sizeof(i); ++byte)
                body[byte] = static_cast<std::uint8_t>(i >> (byte * 8U));
            block.transactions.push_back({2U, std::move(body)});
        }
        block.header.transactions_root = transaction_root(block.transactions);
        if (encode_block(block).size() > max_serialized_block_bytes)
            throw std::runtime_error("bandwidth block exceeds consensus ceiling");

        Block limit_block = block;
        constexpr std::size_t limit_transaction_count = 1811U;
        for (std::size_t i = transaction_count;
             i < limit_transaction_count; ++i) {
            std::vector<std::uint8_t> body(transaction_body_bytes, 0U);
            for (std::size_t byte = 0U; byte < sizeof(i); ++byte)
                body[byte] = static_cast<std::uint8_t>(i >> (byte * 8U));
            limit_block.transactions.push_back({2U, std::move(body)});
        }
        limit_block.header.transactions_root =
            transaction_root(limit_block.transactions);
        const auto encoded_block_bytes = encode_block(limit_block).size();
        if (encoded_block_bytes < max_serialized_block_bytes * 99U / 100U ||
            encoded_block_bytes > max_serialized_block_bytes)
            throw std::runtime_error("limit block is outside the 99 percent window");

        const auto started = std::chrono::steady_clock::now();
        ValidatedRelayPool pool(limit_transaction_count,
                                max_serialized_block_bytes);
        BlockChunkLimits chunk_limits;
        chunk_limits.maximum_chunk_bytes = chunk_bytes;
        chunk_limits.maximum_transactions_per_chunk = 64U;
        chunk_limits.maximum_transaction_body_bytes = 16U * 1024U;
        chunk_limits.maximum_total_chunks = 128U;
        chunk_limits.maximum_total_transactions = limit_transaction_count;
        chunk_limits.maximum_total_transfer_bytes = max_serialized_block_bytes;
        CompactDownloadResult immediate;
        CompactBlockDownload download(make_compact_block_announcement(limit_block),
            pool, chunk_limits, [](const TransactionEnvelope& transaction) {
                return transaction.version == 2U &&
                       transaction.body.size() == transaction_body_bytes;
            });
        const auto request = download.start(immediate);
        if (!request || immediate.error != CompactDownloadError::none)
            throw std::runtime_error("benchmark missing set was not created");
        const auto chunks = make_block_transaction_chunks(
            limit_block, request->indexes, chunk_bytes);
        if (!chunks) throw std::runtime_error("benchmark chunking failed");
        CompactDownloadResult completed;
        for (const auto& chunk : *chunks) {
            completed = download.add_chunk(chunk);
            if (completed.error != CompactDownloadError::none)
                throw std::runtime_error("benchmark reconstruction failed");
        }
        if (!completed.block ||
            encode_block(*completed.block) != encode_block(limit_block))
            throw std::runtime_error("benchmark block mismatch");
        const BlockValidationLimits validation_limits{
            7U, 2U, max_serialized_block_bytes, 4096U, 16U * 1024U};
        const BlockContext context{1U, {}, 0U, 1'800'000'000ULL, 0U,
                                   0x207fffffU};
        if (validate_block(*completed.block, validation_limits, context,
                [](const BlockHeader&) { return true; }) !=
                BlockValidationError::none)
            throw std::runtime_error("benchmark full validation failed");
        const auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        std::cout << "local_block_limit_gate=PASS"
                  << " block_bytes=" << encoded_block_bytes
                  << " chunks=" << chunks->size()
                  << " reconstruction_validation_seconds=" << std::fixed
                  << std::setprecision(6) << elapsed << '\n';

        std::cout << "Onuros Stage 7 compact-relay bandwidth benchmark\n"
                  << "transactions=" << transaction_count
                  << " transaction_bytes="
                  << encode_transaction(block.transactions.front()).size()
                  << " full_block_bytes=" << encode_block(block).size() << "\n\n"
                  << "overlap,announcement_bytes,request_bytes,response_bytes,"
                     "block_phase_bytes,saving_percent\n";
        for (const auto overlap : {25U, 50U, 90U, 100U}) {
            const auto available = transaction_count * overlap / 100U;
            const auto measurement = measure_compact_relay(
                block, available, chunk_bytes);
            if (!measurement) throw std::runtime_error("measurement failed");
            const auto compact = measurement->compact_phase_bytes();
            const double saving = measurement->full_block_bytes == 0U ? 0.0 :
                100.0 * (1.0 - static_cast<double>(compact) /
                                  static_cast<double>(measurement->full_block_bytes));
            std::cout << overlap << ',' << measurement->announcement_bytes << ','
                      << measurement->request_bytes << ','
                      << measurement->response_bytes << ',' << compact << ','
                      << std::fixed << std::setprecision(2) << saving << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
