#pragma once

#include "onuros/block_format.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace onuros {

struct MiniNodePolicy {
    std::uint64_t retained_body_depth = 1440U;
    std::uint64_t checkpoint_interval = 10'080U;
    std::size_t maximum_headers = 5'000'000U;
    std::size_t maximum_retained_body_bytes = 512U * 1024U * 1024U;
};

struct MiniNodeCheckpoint {
    Height height = 0U;
    Hash256 block_identifier{};
    Hash256 shielded_root{};
    Hash256 header_chain_commitment{};
};

struct RetainedBlockBody {
    Height height = 0U;
    Hash256 block_identifier{};
    std::vector<std::uint8_t> encoded_block;
};

enum class MiniNodeHistoryError {
    none,
    invalid_policy,
    header_limit,
    body_limit,
    non_contiguous,
    invalid_transaction_root
};

class MiniNodeHistory {
    MiniNodePolicy policy_;
    std::vector<BlockHeader> headers_;
    std::deque<RetainedBlockBody> bodies_;
    std::vector<MiniNodeCheckpoint> checkpoints_;
    std::size_t retained_body_bytes_ = 0U;
    Hash256 header_chain_commitment_{};

    void prune(Height tip_height) {
        while (!bodies_.empty()) {
            const auto height = bodies_.front().height;
            const bool outside_window = height <= tip_height &&
                policy_.retained_body_depth <= tip_height - height;
            if (!outside_window) break;
            retained_body_bytes_ -= bodies_.front().encoded_block.size();
            bodies_.pop_front();
        }
    }

public:
    explicit MiniNodeHistory(MiniNodePolicy policy) : policy_(policy) {}

    // This is retention, not consensus admission. Call it only after the full
    // block and shielded-state validators accept the active-chain block.
    MiniNodeHistoryError record_after_validation(const Block& block) {
        if (policy_.retained_body_depth == 0U ||
            policy_.checkpoint_interval == 0U || policy_.maximum_headers == 0U)
            return MiniNodeHistoryError::invalid_policy;
        if (headers_.size() >= policy_.maximum_headers)
            return MiniNodeHistoryError::header_limit;
        if (block.header.height != headers_.size() ||
            (!headers_.empty() &&
             block.header.previous != block_id(headers_.back())) ||
            (headers_.empty() && block.header.previous != Hash256{}))
            return MiniNodeHistoryError::non_contiguous;
        if (!has_valid_transaction_root(block))
            return MiniNodeHistoryError::invalid_transaction_root;
        auto encoded = encode_block(block);
        if (encoded.size() > policy_.maximum_retained_body_bytes)
            return MiniNodeHistoryError::body_limit;

        std::vector<std::uint8_t> commitment_input;
        commitment_input.reserve(32U + block_header_encoded_size);
        detail::append_hash(commitment_input, header_chain_commitment_);
        const auto encoded_header = encode_block_header(block.header);
        commitment_input.insert(commitment_input.end(), encoded_header.begin(),
                                encoded_header.end());
        const auto next_commitment = double_sha256(commitment_input);

        prune(block.header.height);
        while (!bodies_.empty() &&
               encoded.size() > policy_.maximum_retained_body_bytes -
                   std::min(policy_.maximum_retained_body_bytes,
                            retained_body_bytes_)) {
            retained_body_bytes_ -= bodies_.front().encoded_block.size();
            bodies_.pop_front();
        }
        if (encoded.size() > policy_.maximum_retained_body_bytes -
                std::min(policy_.maximum_retained_body_bytes,
                         retained_body_bytes_))
            return MiniNodeHistoryError::body_limit;

        headers_.push_back(block.header);
        retained_body_bytes_ += encoded.size();
        bodies_.push_back({block.header.height, block_id(block.header),
                           std::move(encoded)});
        header_chain_commitment_ = next_commitment;
        if (block.header.height % policy_.checkpoint_interval == 0U)
            checkpoints_.push_back({block.header.height, block_id(block.header),
                                    block.header.shielded_root,
                                    header_chain_commitment_});
        prune(block.header.height);
        return MiniNodeHistoryError::none;
    }

    bool has_body(Height height) const {
        return std::any_of(bodies_.begin(), bodies_.end(),
            [height](const RetainedBlockBody& body) {
                return body.height == height;
            });
    }

    bool can_reorganize_from(Height fork_height) const {
        if (headers_.empty() || fork_height >= headers_.size()) return false;
        for (Height height = fork_height + 1U; height < headers_.size(); ++height)
            if (!has_body(height)) return false;
        return true;
    }

    const std::vector<BlockHeader>& headers() const { return headers_; }
    const std::deque<RetainedBlockBody>& retained_bodies() const { return bodies_; }
    const std::vector<MiniNodeCheckpoint>& checkpoints() const {
        return checkpoints_;
    }
    std::size_t retained_body_bytes() const noexcept {
        return retained_body_bytes_;
    }
};

} // namespace onuros
