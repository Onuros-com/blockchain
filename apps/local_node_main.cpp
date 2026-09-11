#include "onuros/local_node.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace onuros;

namespace {

Hash256 deterministic_test_pow(const BlockHeader& header) {
    return double_sha256(encode_block_header(header));
}

void usage(const char* program) {
    std::cout << "Usage: " << program
              << " [--data PATH] [--blocks COUNT]\n"
              << "Runs the Stage 5 local-node pipeline with deterministic CPU test PoW.\n";
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path data_path = "onuros-local-node.db";
    std::uint64_t blocks_to_mine = 10U;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--help") {
                usage(argv[0]);
                return 0;
            }
            if (argument == "--data" && i + 1 < argc) {
                data_path = argv[++i];
            } else if (argument == "--blocks" && i + 1 < argc) {
                const std::string count = argv[++i];
                if (count.empty() || count.front() == '-')
                    throw std::invalid_argument("invalid block count");
                blocks_to_mine = std::stoull(count);
            } else {
                throw std::invalid_argument("unknown or incomplete argument: " + argument);
            }
        }

        std::error_code filesystem_error;
        const auto parent = data_path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, filesystem_error);
            if (filesystem_error)
                throw std::runtime_error("cannot create database directory");
        }

        const auto proof_of_work_limit = decode_compact_target(0x207fffffU);
        if (!proof_of_work_limit)
            throw std::runtime_error("invalid local proof-of-work limit");
        LocalNodeParameters parameters;
        parameters.validation_limits = {1U, 1U, 4U * 1024U * 1024U,
                                        10'000U, 1024U * 1024U};
        parameters.decode_limits = {4U * 1024U * 1024U, 10'000U,
                                    1024U * 1024U};
        parameters.difficulty.target_block_seconds = 60U;
        parameters.difficulty.retarget_interval = 60U;
        parameters.difficulty.adjustment_clamp_factor = 4U;
        parameters.difficulty.proof_of_work_limit = *proof_of_work_limit;
        parameters.max_future_seconds = 120U;
        parameters.max_database_bytes = 1024ULL * 1024ULL * 1024ULL;

        LocalNode node(parameters, deterministic_test_pow);
        const auto opened = node.open(data_path);
        if (opened.error != LocalNodeError::none)
            throw std::runtime_error("database open/revalidation failed");

        std::uint64_t next_timestamp = 1'700'000'000U;
        if (const auto* tip = node.store().index().active_tip()) {
            const auto* stored = node.store().find(tip->id);
            if (stored == nullptr ||
                stored->block.header.timestamp >
                    std::numeric_limits<std::uint64_t>::max() - 60U)
                throw std::runtime_error("invalid recovered tip timestamp");
            next_timestamp = stored->block.header.timestamp + 60U;
        }

        const auto started = std::chrono::steady_clock::now();
        for (std::uint64_t i = 0U; i < blocks_to_mine; ++i) {
            std::vector<std::uint8_t> body(8U);
            for (std::size_t byte = 0U; byte < body.size(); ++byte)
                body[byte] = static_cast<std::uint8_t>(i >> (byte * 8U));
            auto candidate = node.make_candidate({{1U, std::move(body)}}, next_timestamp);
            if (!candidate)
                throw std::runtime_error("could not construct next block");
            const auto mined = node.mine(*candidate, next_timestamp, 1'000'000U);
            if (mined.error != LocalNodeError::none)
                throw std::runtime_error("mining or block admission failed");
            if (next_timestamp > std::numeric_limits<std::uint64_t>::max() - 60U)
                throw std::runtime_error("timestamp exhausted");
            next_timestamp += 60U;
        }
        const auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        const auto* tip = node.store().index().active_tip();

        std::cout << "Onuros Stage 5 local node completed\n"
                  << "Database: " << data_path.string() << '\n'
                  << "Stored blocks: " << node.store().blocks().size() << '\n';
        if (tip != nullptr) {
            std::cout << "Active height: " << tip->height << '\n'
                      << "Active tip: " << hash_hex(tip->id) << '\n';
        }
        std::cout << "This run mined: " << blocks_to_mine << " blocks in "
                  << elapsed << " seconds\n"
                  << "PoW mode: deterministic CPU test engine (RTX GPU is not used yet)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        usage(argv[0]);
        return 1;
    }
}
