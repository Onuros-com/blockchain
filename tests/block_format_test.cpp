#include "onuros/block_format.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace onuros;

namespace {

unsigned checks = 0;

void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}

BlockHeader sample_header() {
    BlockHeader header;
    header.version = 3;
    header.height = 42;
    header.previous[0] = 0xa5;
    header.transactions_root[31] = 0x5a;
    header.shielded_root[15] = 0x3c;
    header.timestamp = 1'800'000'000ULL;
    header.compact_target = 0x1d00ffffU;
    header.nonce = 0x0123456789abcdefULL;
    header.mix_hash[7] = 0x77;
    return header;
}

} // namespace

int main() {
    try {
        const std::string abc = "abc";
        const auto abc_hash = sha256(
            reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size());
        check(hash_hex(abc_hash) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA-256 standard vector");
        const std::vector<std::uint8_t> empty;
        check(hash_hex(sha256(empty)) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "SHA-256 empty vector");

        const TransactionEnvelope first{1, {0x10, 0x20, 0x30}};
        auto changed = first;
        changed.body[1] ^= 1U;
        check(transaction_id(first) != transaction_id(changed),
              "transaction payload binds identifier");
        changed = first;
        ++changed.version;
        check(transaction_id(first) != transaction_id(changed),
              "transaction version binds identifier");
        const auto encoded_transaction = encode_transaction(first);
        check(encoded_transaction.size() == 11U && encoded_transaction[0] == 1U &&
              encoded_transaction[4] == 3U && encoded_transaction[8] == 0x10U,
              "transaction canonical little-endian encoding");

        check(transaction_root({}) == Hash256{}, "empty transaction root is explicit zero");
        check(transaction_root({first}) == transaction_id(first),
              "single transaction root equals transaction identifier");
        const TransactionEnvelope second{1, {0x40}};
        const TransactionEnvelope third{2, {0x50, 0x60}};
        const auto odd_root = transaction_root({first, second, third});
        check(odd_root == transaction_root({first, second, third}),
              "odd transaction tree is deterministic");
        check(odd_root != transaction_root({first, third, second}),
              "transaction order binds root");

        auto header = sample_header();
        const auto encoded_header = encode_block_header(header);
        check(encoded_header.size() == block_header_encoded_size,
              "fixed block header encoding size");
        check(encoded_header[0] == 3U && encoded_header[4] == 42U,
              "block header canonical little-endian encoding");
        const auto original_id = block_id(header);
        auto mutation = header;
        ++mutation.version;
        check(block_id(mutation) != original_id, "version binds block identifier");
        mutation = header;
        ++mutation.height;
        check(block_id(mutation) != original_id, "height binds block identifier");
        mutation = header;
        mutation.previous[0] ^= 1U;
        check(block_id(mutation) != original_id, "parent binds block identifier");
        mutation = header;
        mutation.transactions_root[0] ^= 1U;
        check(block_id(mutation) != original_id, "transactions bind block identifier");
        mutation = header;
        mutation.shielded_root[0] ^= 1U;
        check(block_id(mutation) != original_id,
              "shielded root binds block identifier");
        mutation = header;
        ++mutation.timestamp;
        check(block_id(mutation) != original_id, "timestamp binds block identifier");
        mutation = header;
        ++mutation.compact_target;
        check(block_id(mutation) != original_id, "target binds block identifier");
        mutation = header;
        ++mutation.nonce;
        check(block_id(mutation) != original_id, "nonce binds block identifier");
        mutation = header;
        mutation.mix_hash[0] ^= 1U;
        check(block_id(mutation) != original_id, "mix hash binds block identifier");

        Block block{header, {first, second}};
        check(!has_valid_transaction_root(block), "wrong transaction root rejected");
        block.header.transactions_root = transaction_root(block.transactions);
        check(has_valid_transaction_root(block), "matching transaction root accepted");
        block.transactions[0].body[0] ^= 1U;
        check(!has_valid_transaction_root(block), "mutated transaction invalidates root");

        block.header.transactions_root = transaction_root(block.transactions);
        const auto encoded_block = encode_block(block);
        const DecodeLimits limits{1024U, 8U, 128U};
        const auto decoded_block = decode_block(encoded_block, limits);
        check(decoded_block.has_value() && encode_block(*decoded_block) == encoded_block,
              "block canonical round trip");
        check(decoded_block->header == block.header &&
              decoded_block->transactions.size() == block.transactions.size(),
              "decoded block fields preserved");

        auto malformed = encoded_block;
        malformed.pop_back();
        check(!decode_block(malformed, limits), "truncated block rejected");
        malformed = encoded_block;
        malformed.push_back(0U);
        check(!decode_block(malformed, limits), "trailing block bytes rejected");
        check(!decode_block(encoded_block, {encoded_block.size() - 1U, 8U, 128U}),
              "oversized encoded block rejected");
        check(!decode_block(encoded_block, {1024U, 1U, 128U}),
              "excess transaction count rejected");
        check(!decode_block(encoded_block, {1024U, 8U, 1U}),
              "excess transaction body rejected");

        check(max_serialized_block_bytes == 16U * 1024U * 1024U,
              "consensus block ceiling is exactly 16 MiB");
        Block boundary_block{header, {{1U, std::vector<std::uint8_t>(
            max_serialized_block_bytes - block_prefix_encoded_size - 8U)}}};
        boundary_block.header.transactions_root =
            transaction_root(boundary_block.transactions);
        auto boundary_encoding = encode_block(boundary_block);
        check(boundary_encoding.size() == max_serialized_block_bytes &&
              decode_block(boundary_encoding,
                           {max_serialized_block_bytes + 1U, 1U,
                            static_cast<std::uint32_t>(max_serialized_block_bytes)}),
              "exactly 16 MiB block remains valid");
        boundary_encoding.push_back(0U);
        check(!decode_block(boundary_encoding,
                            {max_serialized_block_bytes + 1U, 1U,
                             static_cast<std::uint32_t>(max_serialized_block_bytes)}),
              "configured decoder cannot bypass 16 MiB consensus ceiling");

        const auto encoded_first = encode_transaction(first);
        const auto decoded_first = decode_transaction(encoded_first, 128U);
        check(decoded_first.has_value() && encode_transaction(*decoded_first) == encoded_first,
              "transaction canonical round trip");
        malformed = encoded_first;
        malformed.push_back(0U);
        check(!decode_transaction(malformed, 128U), "transaction trailing bytes rejected");
        malformed = encoded_first;
        malformed[4] = 0xffU;
        malformed[5] = 0xffU;
        malformed[6] = 0xffU;
        malformed[7] = 0x7fU;
        check(!decode_transaction(malformed, 128U), "declared huge body rejected");

        std::cout << checks << " block-format checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
