#include "onuros/header_sync.hpp"

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
HeaderSyncLimits limits() {
    const auto pow_limit = decode_compact_target(0x207fffffU);
    if (!pow_limit) throw std::runtime_error("test target");
    HeaderSyncLimits result;
    result.difficulty = {60U, 1'000U, 4U, *pow_limit};
    return result;
}
void mine(BlockHeader& header, const Target256& pow_limit) {
    while (!hash_meets_compact_target(test_pow(header), header.compact_target,
                                      pow_limit)) {
        ++header.nonce;
    }
}
BlockHeader child(const BlockHeader& parent, std::uint64_t timestamp,
                  const Target256& pow_limit, std::uint8_t marker) {
    BlockHeader header;
    header.version = 1U;
    header.height = parent.height + 1U;
    header.previous = block_id(parent);
    header.timestamp = timestamp;
    header.compact_target = parent.compact_target;
    header.transactions_root[0] = marker;
    mine(header, pow_limit);
    return header;
}
}

int main() {
    try {
        const auto configured = limits();
        BlockHeader genesis;
        genesis.version = 1U;
        genesis.timestamp = 100U;
        genesis.compact_target = 0x207fffffU;
        mine(genesis, configured.difficulty.proof_of_work_limit);

        HeaderRequest request{{block_id(genesis)}, {}};
        const auto request_round_trip = decode_header_request(
            encode_header_request(request), 32U);
        check(request_round_trip && request_round_trip->locator == request.locator,
              "header locator canonical round trip");
        check(!decode_header_request(encode_header_request(request), 0U),
              "header locator count bounded");

        HeaderSyncChain chain(configured, test_pow);
        const auto genesis_work = chain_work_from_target_work(
            *work_for_compact_target(genesis.compact_target,
                configured.difficulty.proof_of_work_limit));
        check(chain.seed(genesis, genesis_work) == HeaderSyncError::none,
              "fully validated genesis seeds header chain");

        const auto first = child(genesis, 160U,
            configured.difficulty.proof_of_work_limit, 1U);
        const auto second = child(first, 220U,
            configured.difficulty.proof_of_work_limit, 2U);
        const std::vector<BlockHeader> main_headers{first, second};
        const auto wire_headers = decode_headers(encode_headers(main_headers), 2U);
        check(wire_headers && *wire_headers == main_headers,
              "header batch canonical round trip");
        const auto accepted = chain.accept(main_headers, 220U);
        check(accepted.error == HeaderSyncError::none && accepted.accepted == 2U &&
              accepted.stronger_tip && chain.best_tip()->id == block_id(second),
              "multi-height header chain selects cumulative-work tip");

        auto weak_fork = child(genesis, 161U,
            configured.difficulty.proof_of_work_limit, 3U);
        const auto weak = chain.accept({weak_fork}, 220U);
        check(weak.error == HeaderSyncError::none && !weak.stronger_tip &&
              chain.best_tip()->id == block_id(second),
              "shorter fork does not trigger block download");

        const auto fork_second = child(weak_fork, 221U,
            configured.difficulty.proof_of_work_limit, 4U);
        const auto fork_third = child(fork_second, 281U,
            configured.difficulty.proof_of_work_limit, 5U);
        const auto stronger = chain.accept({fork_second, fork_third}, 281U);
        check(stronger.error == HeaderSyncError::none && stronger.stronger_tip &&
              chain.best_tip()->id == block_id(fork_third),
              "longer valid fork wins by cumulative work");

        const auto size_before_rejection = chain.size();
        const auto valid_next = child(fork_third, 341U,
            configured.difficulty.proof_of_work_limit, 6U);
        auto invalid_next = child(valid_next, 401U,
            configured.difficulty.proof_of_work_limit, 7U);
        invalid_next.previous[0] ^= 1U;
        const auto rejected = chain.accept({valid_next, invalid_next}, 401U);
        check(rejected.error == HeaderSyncError::unknown_parent &&
              chain.size() == size_before_rejection &&
              chain.find(block_id(valid_next)) == nullptr,
              "invalid batch rolls back atomically");

        std::cout << checks << " header-sync checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
