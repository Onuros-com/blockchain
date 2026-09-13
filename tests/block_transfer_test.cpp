#include "onuros/block_transfer.hpp"

#include <iostream>
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
    block.header.height = 9U;
    for (std::uint8_t i = 0U; i < 6U; ++i)
        block.transactions.push_back({2U, std::vector<std::uint8_t>(20U, i)});
    block.header.transactions_root = transaction_root(block.transactions);
    return block;
}
}

int main() {
    try {
        const auto block = sample_block();
        const MissingTransactionRequest request{
            block_id(block.header), {1U, 3U, 5U}};
        const auto request_bytes = encode_missing_transaction_request(request);
        const auto decoded_request = decode_missing_transaction_request(
            request_bytes, 4U);
        check(decoded_request && decoded_request->indexes == request.indexes,
              "missing request canonical round trip");
        check(!decode_missing_transaction_request(request_bytes, 2U),
              "missing request count bounded");
        auto malformed_request = request;
        malformed_request.indexes = {3U, 1U};
        check(!decode_missing_transaction_request(
                  encode_missing_transaction_request(malformed_request), 4U),
              "missing request indexes must be ordered and unique");

        const auto chunks = make_block_transaction_chunks(
            block, request.indexes, 85U);
        check(chunks && chunks->size() == 3U,
              "bounded builder separates oversized response");
        BlockChunkLimits limits;
        limits.maximum_chunk_bytes = 85U;
        limits.maximum_transactions_per_chunk = 2U;
        limits.maximum_transaction_body_bytes = 64U;
        limits.maximum_total_chunks = 8U;
        limits.maximum_total_transactions = 8U;
        limits.maximum_total_transfer_bytes = 1024U;
        for (const auto& chunk : *chunks) {
            const auto bytes = encode_block_transaction_chunk(chunk);
            const auto decoded = decode_block_transaction_chunk(bytes, limits);
            check(decoded && decoded->sequence == chunk.sequence &&
                  decoded->transactions.size() == 1U,
                  "transaction chunk canonical bounded round trip");
        }

        BlockTransactionAssembler assembler(block_id(block.header), limits);
        for (const auto& chunk : *chunks)
            check(assembler.add(chunk) == BlockChunkAssemblyError::none,
                  "ordered chunk accepted");
        check(assembler.complete() && assembler.transactions().size() == 3U,
              "all requested transactions assembled");

        BlockTransactionAssembler out_of_order(block_id(block.header), limits);
        check(out_of_order.add((*chunks)[1]) ==
                  BlockChunkAssemblyError::out_of_order,
              "out-of-order first chunk rejected");
        auto wrong_block = (*chunks)[0];
        wrong_block.block_identifier[0] ^= 1U;
        check(out_of_order.add(wrong_block) == BlockChunkAssemblyError::wrong_block,
              "wrong block chunk rejected");

        auto inconsistent = (*chunks)[0];
        BlockTransactionAssembler inconsistent_assembler(
            block_id(block.header), limits);
        check(inconsistent_assembler.add(inconsistent) ==
                  BlockChunkAssemblyError::none,
              "first manifest accepted");
        inconsistent = (*chunks)[1];
        ++inconsistent.total_transfer_bytes;
        check(inconsistent_assembler.add(inconsistent) ==
                  BlockChunkAssemblyError::inconsistent_manifest,
              "manifest mutation rejected");

        BlockTransactionAssembler extra_assembler(block_id(block.header), limits);
        check(extra_assembler.add((*chunks)[0]) == BlockChunkAssemblyError::none,
              "bounded sequence starts at zero");
        auto impossible_extra = (*chunks)[0];
        impossible_extra.sequence = impossible_extra.total_chunks;
        check(extra_assembler.add(impossible_extra) ==
                  BlockChunkAssemblyError::out_of_order,
              "chunk beyond declared sequence rejected");

        auto bytes = encode_block_transaction_chunk((*chunks)[0]);
        bytes.push_back(0U);
        check(!decode_block_transaction_chunk(bytes, limits),
              "chunk trailing bytes rejected");
        check(!make_block_transaction_chunks(block, {6U}, 85U),
              "out-of-range transaction request rejected");

        const ArchiveBlockRequest archive_request{block_id(block.header)};
        const auto encoded_archive_request =
            encode_archive_block_request(archive_request);
        const auto decoded_archive_request =
            decode_archive_block_request(encoded_archive_request);
        check(decoded_archive_request &&
                  decoded_archive_request->block_identifier ==
                      archive_request.block_identifier,
              "archive request canonical round trip");
        auto trailing_request = encoded_archive_request;
        trailing_request.push_back(0U);
        check(!decode_archive_block_request(trailing_request),
              "archive request rejects trailing data");

        const auto archive_chunks = make_archive_block_chunks(block, 220U);
        check(archive_chunks && archive_chunks->size() > 1U,
              "archive response uses bounded chunks");
        ArchiveBlockLimits archive_limits;
        archive_limits.maximum_chunk_bytes = 220U;
        archive_limits.maximum_total_chunks = 16U;
        archive_limits.maximum_block_bytes = 4096U;
        ArchiveBlockAssembler archive_assembler(
            block.header, {4096U, 16U, 64U}, archive_limits);
        ArchiveBlockAssemblyResult archive_result;
        for (const auto& chunk : *archive_chunks) {
            const auto encoded_chunk = encode_archive_block_chunk(chunk);
            const auto decoded_chunk = decode_archive_block_chunk(
                encoded_chunk, archive_limits);
            check(decoded_chunk.has_value(),
                  "archive chunk canonical bounded round trip");
            archive_result = archive_assembler.add(*decoded_chunk);
            check(archive_result.error == ArchiveBlockAssemblyError::none,
                  "ordered archive chunk accepted");
        }
        check(archive_result.block &&
                  encode_block(*archive_result.block) == encode_block(block),
              "archive chunks reconstruct exact committed block");

        ArchiveBlockAssembler archive_out_of_order(
            block.header, {4096U, 16U, 64U}, archive_limits);
        check(archive_out_of_order.add((*archive_chunks)[1]).error ==
                  ArchiveBlockAssemblyError::out_of_order,
              "out-of-order archive chunk rejected");
        auto changed_manifest = (*archive_chunks)[0];
        ++changed_manifest.total_block_bytes;
        ArchiveBlockAssembler archive_manifest(
            block.header, {4096U, 16U, 64U}, archive_limits);
        check(archive_manifest.add(changed_manifest).error ==
                  ArchiveBlockAssemblyError::none,
              "first archive manifest initializes bounded assembly");
        changed_manifest = (*archive_chunks)[1];
        check(archive_manifest.add(changed_manifest).error ==
                  ArchiveBlockAssemblyError::inconsistent_manifest,
              "inconsistent archive manifest rejected");

        auto corrupt_chunks = *archive_chunks;
        corrupt_chunks.back().bytes.back() ^= 1U;
        ArchiveBlockAssembler corrupt_archive(
            block.header, {4096U, 16U, 64U}, archive_limits);
        ArchiveBlockAssemblyResult corrupt_result;
        for (const auto& chunk : corrupt_chunks)
            corrupt_result = corrupt_archive.add(chunk);
        check(corrupt_result.error == ArchiveBlockAssemblyError::invalid_block,
              "archive body not matching committed transaction root rejected");

        std::cout << checks << " block-transfer checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
