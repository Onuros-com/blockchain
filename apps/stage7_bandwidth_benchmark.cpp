#include "onuros/stage7_relay.hpp"

#include <cstddef>
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
        block.transactions.reserve(transaction_count);
        for (std::size_t i = 0U; i < transaction_count; ++i) {
            std::vector<std::uint8_t> body(transaction_body_bytes, 0U);
            for (std::size_t byte = 0U; byte < sizeof(i); ++byte)
                body[byte] = static_cast<std::uint8_t>(i >> (byte * 8U));
            block.transactions.push_back({2U, std::move(body)});
        }
        block.header.transactions_root = transaction_root(block.transactions);
        if (encode_block(block).size() > max_serialized_block_bytes)
            throw std::runtime_error("benchmark block exceeds consensus ceiling");

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
