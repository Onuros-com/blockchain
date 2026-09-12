#include "onuros/local_node.hpp"
#include "onuros/stage7_relay.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace onuros;

namespace {
unsigned checks = 0U;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}
Hash256 test_pow(const BlockHeader& header) {
    return double_sha256(encode_block_header(header));
}
LocalNodeParameters parameters() {
    const auto limit = decode_compact_target(0x207fffffU);
    if (!limit) throw std::runtime_error("test target");
    LocalNodeParameters value;
    value.validation_limits = {1U, 1U, 1U << 20U, 512U, 4096U};
    value.decode_limits = {1U << 20U, 512U, 4096U};
    value.difficulty = {60U, 60U, 4U, *limit};
    value.max_database_bytes = 8U << 20U;
    return value;
}
std::filesystem::path path(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}
void erase(const std::filesystem::path& value) {
    std::error_code ignored;
    std::filesystem::remove(value, ignored);
    auto temporary = value;
    temporary += ".tmp";
    std::filesystem::remove(temporary, ignored);
}
}

int main() {
    const auto first_path = path("onuros-stage7-node-a.db");
    const auto second_path = path("onuros-stage7-node-b.db");
    const auto third_path = path("onuros-stage7-node-c.db");
    erase(first_path); erase(second_path); erase(third_path);
    try {
        LocalNode first(parameters(), test_pow);
        LocalNode second(parameters(), test_pow);
        LocalNode third(parameters(), test_pow);
        check(first.open(first_path).error == LocalNodeError::none &&
              second.open(second_path).error == LocalNodeError::none &&
              third.open(third_path).error == LocalNodeError::none,
              "three isolated nodes open");

        auto genesis = first.make_candidate({{1U, {0U}}}, 100U);
        check(genesis && first.mine(*genesis, 100U, 10'000U).error ==
                  LocalNodeError::none,
              "first node mines genesis");
        check(second.submit(*genesis, 100U).error == LocalNodeError::none &&
              third.submit(*genesis, 100U).error == LocalNodeError::none,
              "genesis converges across three nodes");

        std::vector<TransactionEnvelope> transactions;
        for (std::uint16_t i = 0U; i < 100U; ++i) {
            std::vector<std::uint8_t> body(128U, static_cast<std::uint8_t>(i));
            body[0] = static_cast<std::uint8_t>(i & 0xffU);
            transactions.push_back({1U, std::move(body)});
        }
        auto candidate = first.make_candidate(transactions, 160U);
        check(candidate && first.mine(*candidate, 160U, 100'000U).error ==
                  LocalNodeError::none,
              "first node mines relayed block");
        const auto announcement = make_compact_block_announcement(*candidate);

        ValidatedRelayPool second_pool(200U, 1U << 20U);
        ValidatedRelayPool third_pool(200U, 1U << 20U);
        for (std::size_t i = 0U; i < 90U; ++i)
            check(second_pool.remember_validated(transactions[i]),
                  "second node preloads validated transaction");
        for (std::size_t i = 0U; i < 25U; ++i)
            check(third_pool.remember_validated(transactions[i]),
                  "third node preloads validated transaction");

        BlockChunkLimits limits;
        limits.maximum_chunk_bytes = 2048U;
        limits.maximum_transactions_per_chunk = 32U;
        limits.maximum_transaction_body_bytes = 4096U;
        limits.maximum_total_chunks = 128U;
        limits.maximum_total_transactions = 128U;
        limits.maximum_total_transfer_bytes = 1U << 20U;

        CompactDownloadResult second_immediate;
        const auto admit_test_transaction = [](const TransactionEnvelope& transaction) {
            return transaction.version == 1U && transaction.body.size() == 128U;
        };
        CompactBlockDownload second_download(
            announcement, second_pool, limits, admit_test_transaction);
        const auto second_request = second_download.start(second_immediate);
        check(second_request && second_request->indexes.size() == 10U,
              "90-percent peer requests only ten missing transactions");
        const auto second_chunks = make_block_transaction_chunks(
            *candidate, second_request->indexes, limits.maximum_chunk_bytes);
        check(second_chunks.has_value(), "second peer response chunked");
        CompactDownloadResult second_result;
        for (const auto& chunk : *second_chunks)
            second_result = second_download.add_chunk(chunk);
        check(second_result.block &&
              encode_block(*second_result.block) == encode_block(*candidate),
              "second peer reconstructs exact block");

        CompactDownloadResult third_immediate;
        CompactBlockDownload third_download(
            announcement, third_pool, limits, admit_test_transaction);
        const auto third_request = third_download.start(third_immediate);
        check(third_request && third_request->indexes.size() == 75U,
              "25-percent peer requests only missing transactions");
        const auto third_chunks = make_block_transaction_chunks(
            *candidate, third_request->indexes, limits.maximum_chunk_bytes);
        check(third_chunks.has_value() && third_chunks->size() > 1U,
              "large missing response uses bounded chunks");
        CompactDownloadResult third_result;
        for (const auto& chunk : *third_chunks)
            third_result = third_download.add_chunk(chunk);
        check(third_result.block &&
              encode_block(*third_result.block) == encode_block(*candidate),
              "third peer reconstructs exact block");

        ValidatedRelayPool rejecting_pool(200U, 1U << 20U);
        CompactDownloadResult rejecting_immediate;
        CompactBlockDownload rejecting_download(
            announcement, rejecting_pool, limits,
            [](const TransactionEnvelope&) { return false; });
        const auto rejecting_request = rejecting_download.start(rejecting_immediate);
        check(rejecting_request.has_value(),
              "empty peer requests missing block transactions");
        const auto rejecting_chunks = make_block_transaction_chunks(
            *candidate, rejecting_request->indexes, limits.maximum_chunk_bytes);
        check(rejecting_chunks.has_value(), "rejection response chunked");
        CompactDownloadResult rejecting_result;
        for (const auto& chunk : *rejecting_chunks) {
            rejecting_result = rejecting_download.add_chunk(chunk);
            if (rejecting_result.error != CompactDownloadError::none) break;
        }
        check(rejecting_result.error ==
                  CompactDownloadError::transaction_admission_failed &&
              rejecting_pool.size() == 0U,
              "downloaded transactions require local admission atomically");

        check(second.submit(*second_result.block, 160U).error ==
                  LocalNodeError::none &&
              third.submit(*third_result.block, 160U).error ==
                  LocalNodeError::none,
              "all nodes independently validate reconstructed block");
        check(first.active_state().tip() == second.active_state().tip() &&
              second.active_state().tip() == third.active_state().tip(),
              "three nodes converge on identical active tip");

        LocalNode restarted(parameters(), test_pow);
        check(restarted.open(third_path).error == LocalNodeError::none &&
              restarted.active_state().tip() == first.active_state().tip(),
              "late restart recovers converged tip");

        const auto bandwidth = measure_compact_relay(*candidate, 90U, 2048U);
        check(bandwidth && bandwidth->compact_phase_bytes() <
                  bandwidth->full_block_bytes,
              "compact relay uses fewer block-phase bytes at high overlap");

        std::cout << checks << " Stage 7 relay/convergence checks passed\n";
    } catch (const std::exception& error) {
        erase(first_path); erase(second_path); erase(third_path);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    erase(first_path); erase(second_path); erase(third_path);
}
