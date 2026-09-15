#pragma once

#include "onuros/consensus_limits.hpp"
#include "onuros/economics.hpp"
#include "onuros/hash256.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace onuros {

// The envelope remains generic. Stage 6 defines the version-2 private body in
// private_transaction.hpp while preserving the Stage 5 block encoding.
struct TransactionEnvelope {
    std::uint32_t version = 1;
    std::vector<std::uint8_t> body;
};

struct BlockHeader {
    std::uint32_t version = 1;
    Height height = 0;
    Hash256 previous{};
    Hash256 transactions_root{};
    // Consensus commitment to the active Poseidon note tree after applying
    // this block. This is part of the block id and PoW preimage.
    Hash256 shielded_root{};
    std::uint64_t timestamp = 0;
    std::uint32_t compact_target = 0;
    std::uint64_t nonce = 0;
    Hash256 mix_hash{};
};

inline bool operator==(const BlockHeader& left, const BlockHeader& right) {
    return left.version == right.version && left.height == right.height &&
           left.previous == right.previous &&
           left.transactions_root == right.transactions_root &&
           left.shielded_root == right.shielded_root &&
           left.timestamp == right.timestamp &&
           left.compact_target == right.compact_target &&
           left.nonce == right.nonce && left.mix_hash == right.mix_hash;
}

struct Block {
    BlockHeader header;
    std::vector<TransactionEnvelope> transactions;
};

inline constexpr std::size_t block_header_encoded_size = 160U;
inline constexpr std::size_t block_prefix_encoded_size =
    block_header_encoded_size + sizeof(std::uint32_t);

struct DecodeLimits {
    std::size_t max_block_bytes;
    std::uint32_t max_transactions;
    std::uint32_t max_transaction_body_bytes;
};

namespace detail {

template <typename Integer>
inline void append_little_endian(std::vector<std::uint8_t>& output, Integer value) {
    for (std::size_t i = 0; i < sizeof(Integer); ++i)
        output.push_back(static_cast<std::uint8_t>(value >> (i * 8U)));
}

inline void append_hash(std::vector<std::uint8_t>& output, const Hash256& hash) {
    output.insert(output.end(), hash.begin(), hash.end());
}

class ByteReader {
    const std::vector<std::uint8_t>& input_;
    std::size_t position_ = 0;
public:
    explicit ByteReader(const std::vector<std::uint8_t>& input) : input_(input) {}

    std::size_t remaining() const { return input_.size() - position_; }
    bool exhausted() const { return position_ == input_.size(); }

    template <typename Integer>
    bool read_little_endian(Integer& value) {
        if (remaining() < sizeof(Integer)) return false;
        value = 0;
        for (std::size_t i = 0; i < sizeof(Integer); ++i)
            value |= static_cast<Integer>(input_[position_ + i]) << (i * 8U);
        position_ += sizeof(Integer);
        return true;
    }

    bool read_hash(Hash256& hash) {
        if (remaining() < hash.size()) return false;
        for (std::size_t i = 0; i < hash.size(); ++i) hash[i] = input_[position_ + i];
        position_ += hash.size();
        return true;
    }

    bool read_bytes(std::size_t size, std::vector<std::uint8_t>& bytes) {
        if (size > remaining()) return false;
        bytes.assign(input_.begin() + static_cast<std::ptrdiff_t>(position_),
                     input_.begin() + static_cast<std::ptrdiff_t>(position_ + size));
        position_ += size;
        return true;
    }
};

inline bool read_transaction(ByteReader& reader, std::uint32_t max_body_bytes,
                             TransactionEnvelope& transaction) {
    std::uint32_t body_size = 0;
    if (!reader.read_little_endian(transaction.version) ||
        !reader.read_little_endian(body_size) || body_size > max_body_bytes)
        return false;
    return reader.read_bytes(body_size, transaction.body);
}

inline bool read_block_header(ByteReader& reader, BlockHeader& header) {
    return reader.read_little_endian(header.version) &&
           reader.read_little_endian(header.height) &&
           reader.read_hash(header.previous) &&
           reader.read_hash(header.transactions_root) &&
           reader.read_hash(header.shielded_root) &&
           reader.read_little_endian(header.timestamp) &&
           reader.read_little_endian(header.compact_target) &&
           reader.read_little_endian(header.nonce) &&
           reader.read_hash(header.mix_hash);
}

} // namespace detail

inline std::vector<std::uint8_t> encode_transaction(const TransactionEnvelope& transaction) {
    if (transaction.body.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("transaction body exceeds canonical length field");
    std::vector<std::uint8_t> output;
    output.reserve(8U + transaction.body.size());
    detail::append_little_endian(output, transaction.version);
    detail::append_little_endian(output,
        static_cast<std::uint32_t>(transaction.body.size()));
    output.insert(output.end(), transaction.body.begin(), transaction.body.end());
    return output;
}

inline Hash256 transaction_id(const TransactionEnvelope& transaction) {
    return double_sha256(encode_transaction(transaction));
}

inline Hash256 transaction_root(const std::vector<TransactionEnvelope>& transactions) {
    if (transactions.empty()) return {};
    std::vector<Hash256> level;
    level.reserve(transactions.size());
    for (const auto& transaction : transactions) level.push_back(transaction_id(transaction));
    while (level.size() > 1U) {
        if ((level.size() & 1U) != 0U) level.push_back(level.back());
        std::vector<Hash256> next;
        next.reserve(level.size() / 2U);
        for (std::size_t i = 0; i < level.size(); i += 2U) {
            std::vector<std::uint8_t> pair;
            pair.reserve(64U);
            detail::append_hash(pair, level[i]);
            detail::append_hash(pair, level[i + 1U]);
            next.push_back(double_sha256(pair));
        }
        level = std::move(next);
    }
    return level.front();
}

inline std::vector<std::uint8_t> encode_block_header(const BlockHeader& header) {
    std::vector<std::uint8_t> output;
    output.reserve(block_header_encoded_size);
    detail::append_little_endian(output, header.version);
    detail::append_little_endian(output, header.height);
    detail::append_hash(output, header.previous);
    detail::append_hash(output, header.transactions_root);
    detail::append_hash(output, header.shielded_root);
    detail::append_little_endian(output, header.timestamp);
    detail::append_little_endian(output, header.compact_target);
    detail::append_little_endian(output, header.nonce);
    detail::append_hash(output, header.mix_hash);
    return output;
}

inline Hash256 block_id(const BlockHeader& header) {
    return double_sha256(encode_block_header(header));
}

inline std::vector<std::uint8_t> encode_block(const Block& block) {
    if (block.transactions.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("transaction count exceeds canonical length field");
    auto output = encode_block_header(block.header);
    detail::append_little_endian(output,
        static_cast<std::uint32_t>(block.transactions.size()));
    for (const auto& transaction : block.transactions) {
        const auto encoded = encode_transaction(transaction);
        output.insert(output.end(), encoded.begin(), encoded.end());
    }
    return output;
}

inline std::optional<TransactionEnvelope> decode_transaction(
        const std::vector<std::uint8_t>& input, std::uint32_t max_body_bytes) {
    detail::ByteReader reader(input);
    TransactionEnvelope transaction;
    if (!detail::read_transaction(reader, max_body_bytes, transaction) ||
        !reader.exhausted())
        return std::nullopt;
    return transaction;
}

inline std::optional<Block> decode_block(const std::vector<std::uint8_t>& input,
                                         DecodeLimits limits) {
    if (input.size() > effective_block_limit(limits.max_block_bytes) ||
        input.size() < block_prefix_encoded_size)
        return std::nullopt;
    detail::ByteReader reader(input);
    Block block;
    std::uint32_t transaction_count = 0;
    if (!detail::read_block_header(reader, block.header) ||
        !reader.read_little_endian(transaction_count) ||
        transaction_count > limits.max_transactions ||
        transaction_count > reader.remaining() / 8U)
        return std::nullopt;
    block.transactions.reserve(transaction_count);
    for (std::uint32_t i = 0; i < transaction_count; ++i) {
        TransactionEnvelope transaction;
        if (!detail::read_transaction(reader, limits.max_transaction_body_bytes,
                                      transaction))
            return std::nullopt;
        block.transactions.push_back(std::move(transaction));
    }
    if (!reader.exhausted()) return std::nullopt;
    return block;
}

inline bool has_valid_transaction_root(const Block& block) {
    return block.header.transactions_root == transaction_root(block.transactions);
}

} // namespace onuros
