#include "onuros/kawpow.hpp"
#include "onuros/local_node.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace onuros;

namespace {
unsigned checks = 0;

void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}

Hash256 from_hex(const std::string& text) {
    if (text.size() != 64U) throw std::runtime_error("bad hash fixture");
    const auto nibble = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
        throw std::runtime_error("bad hex fixture");
    };
    Hash256 result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<std::uint8_t>((nibble(text[i * 2U]) << 4U) |
                                              nibble(text[i * 2U + 1U]));
    return result;
}
} // namespace

int main() {
    try {
        // Published Ravencoin KawPoW vector at block 30,000. This detects
        // algorithm, byte-order, epoch, nonce and mix-regression errors.
        const auto header_hash = from_hex(
            "ffeeddccbbaa9988776655443322110000112233445566778899aabbccddeeff");
        const auto vector = calculate_kawpow(30'000U, header_hash,
                                             0x123456789abcdef0ULL);
        check(vector.has_value(), "published vector calculated");
        check(hash_hex(vector->mix_hash) ==
              "177b565752a375501e11b6d9d3679c2df6197b2cab3a1ba2d6b10b8c71a3d459",
              "published mix hash matches");
        check(hash_hex(vector->final_hash) ==
              "c824bee0418e3cfb7fae56e0d5b3b8b14ba895777feea81c70c0ba947146da69",
              "published final hash matches");

        BlockHeader header;
        header.version = 1U;
        header.height = 0U;
        header.previous[0] = 1U;
        header.transactions_root[1] = 2U;
        header.shielded_root[2] = 3U;
        header.timestamp = 1'700'000'000U;
        header.compact_target = 0x207fffffU;
        header.nonce = 7U;

        const auto preimage = encode_kawpow_header_preimage(header);
        check(preimage.size() == 120U, "canonical preimage size");
        const auto base_header_hash = kawpow_header_hash(header);
        auto non_preimage_mutation = header;
        ++non_preimage_mutation.nonce;
        non_preimage_mutation.mix_hash[0] = 9U;
        check(kawpow_header_hash(non_preimage_mutation) == base_header_hash,
              "nonce and mix excluded from header prehash");
        auto committed_mutation = header;
        ++committed_mutation.timestamp;
        check(kawpow_header_hash(committed_mutation) != base_header_hash,
              "timestamp committed to header prehash");

        const auto calculated = calculate_kawpow(header.height,
                                                  base_header_hash,
                                                  header.nonce);
        check(calculated.has_value(), "candidate proof calculated");
        header.mix_hash = calculated->mix_hash;
        check(kawpow_pow_hash(header) ==
                  std::optional<Hash256>{calculated->final_hash},
              "valid GPU-style result independently accepted");
        header.mix_hash[7] ^= 1U;
        check(!kawpow_pow_hash(header),
              "altered mix independently rejected");

        check(!calculate_kawpow(
                  static_cast<Height>(std::numeric_limits<int>::max()) + 1U,
                  header_hash, 0U),
              "unrepresentable height rejected");

        const auto database = std::filesystem::temp_directory_path() /
                              "onuros-kawpow-admission-test.db";
        auto temporary = database;
        temporary += ".tmp";
        std::error_code ignored;
        std::filesystem::remove(database, ignored);
        std::filesystem::remove(temporary, ignored);

        const auto limit = decode_compact_target(0x207fffffU);
        check(limit.has_value(), "test target decoded");
        LocalNodeParameters parameters;
        parameters.validation_limits = {1U, 1U, 4096U, 16U, 1024U};
        parameters.decode_limits = {4096U, 16U, 1024U};
        parameters.difficulty.proof_of_work_limit = *limit;
        parameters.difficulty.retarget_interval = 60U;
        parameters.max_database_bytes = 1U << 20U;

        LocalNode node(parameters, kawpow_pow_hash);
        check(node.open(database).error == LocalNodeError::none,
              "KawPoW node opens");
        auto candidate = node.make_candidate({{1U, {1U}}}, 100U);
        check(candidate.has_value(), "KawPoW candidate created");
        for (;;) {
            const auto proof = calculate_kawpow(candidate->header.height,
                                                kawpow_header_hash(candidate->header),
                                                candidate->header.nonce);
            check(proof.has_value(), "candidate proof available");
            candidate->header.mix_hash = proof->mix_hash;
            if (hash_meets_compact_target(proof->final_hash,
                    candidate->header.compact_target, *limit))
                break;
            ++candidate->header.nonce;
        }
        auto altered = *candidate;
        altered.header.mix_hash[3] ^= 1U;
        const auto rejected = node.submit(altered, 100U);
        check(rejected.validation_error ==
                  BlockValidationError::invalid_proof_of_work,
              "altered GPU share rejected by node path");
        check(node.submit(*candidate, 100U).error == LocalNodeError::none,
              "CPU-verified KawPoW block admitted");
        check(node.store().blocks().size() == 1U,
              "only verified KawPoW block persisted");

        std::filesystem::remove(database, ignored);
        std::filesystem::remove(temporary, ignored);

        std::cout << checks << " KawPoW consensus checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
