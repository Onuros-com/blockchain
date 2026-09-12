#pragma once

#include "onuros/kawpow.hpp"
#include "onuros/local_node.hpp"
#include "onuros/mining_protocol.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace onuros {

enum class MiningSubmitError {
    none,
    not_open,
    no_active_job,
    stale_job,
    invalid_proof,
    node_rejected
};

struct MiningSubmitResult {
    MiningSubmitError error = MiningSubmitError::none;
    Hash256 block_identifier{};
    LocalNodeResult node_result{};
};

// Keeps full block templates inside the trusted node. External miners receive
// only a bounded job and may propose a nonce/mix pair. LocalNode is permanently
// wired to the CPU KawPoW verifier, so GPU output cannot bypass consensus.
class KawpowMiningEndpoint {
    LocalNode node_;
    std::optional<Block> active_candidate_;
    std::uint64_t active_job_id_ = 0U;
    std::uint64_t next_job_id_ = 1U;
    bool open_ = false;

    bool active_job_extends_tip() const {
        if (!active_candidate_) return false;
        const auto* tip = node_.store().index().active_tip();
        if (active_candidate_->header.height == 0U)
            return tip == nullptr && node_.store().blocks().empty();
        return tip != nullptr &&
               tip->height + 1U == active_candidate_->header.height &&
               tip->id == active_candidate_->header.previous;
    }

public:
    explicit KawpowMiningEndpoint(LocalNodeParameters parameters)
        : node_(std::move(parameters), kawpow_pow_hash) {}

    LocalNodeResult open(const std::filesystem::path& path) {
        const auto result = node_.open(path);
        open_ = result.error == LocalNodeError::none;
        return result;
    }

    std::optional<MiningJob> issue_job(
            std::vector<TransactionEnvelope> transactions,
            std::uint64_t timestamp) {
        if (!open_ || next_job_id_ == std::numeric_limits<std::uint64_t>::max())
            return std::nullopt;
        auto candidate = node_.make_candidate(std::move(transactions), timestamp);
        if (!candidate) return std::nullopt;
        candidate->header.nonce = 0U;
        candidate->header.mix_hash = {};
        active_candidate_ = std::move(candidate);
        active_job_id_ = next_job_id_++;
        return MiningJob{active_job_id_, active_candidate_->header.height,
                         kawpow_header_hash(active_candidate_->header),
                         active_candidate_->header.compact_target};
    }

    MiningSubmitResult submit(const MiningSolution& solution,
                              std::uint64_t adjusted_time) {
        if (!open_) return {MiningSubmitError::not_open};
        if (!active_candidate_) return {MiningSubmitError::no_active_job};
        if (solution.job_id != active_job_id_ || !active_job_extends_tip())
            return {MiningSubmitError::stale_job};
        auto candidate = *active_candidate_;
        candidate.header.nonce = solution.nonce;
        candidate.header.mix_hash = solution.mix_hash;
        const auto result = node_.submit(candidate, adjusted_time);
        if (result.validation_error == BlockValidationError::invalid_proof_of_work)
            return {MiningSubmitError::invalid_proof, {}, result};
        if (result.error != LocalNodeError::none)
            return {MiningSubmitError::node_rejected, {}, result};
        const auto identifier = block_id(candidate.header);
        active_candidate_.reset();
        active_job_id_ = 0U;
        return {MiningSubmitError::none, identifier, result};
    }

    const LocalNode& node() const noexcept { return node_; }
};

inline MiningResultMessage make_mining_result_message(
        std::uint64_t job_id, const MiningSubmitResult& result) {
    MiningResultCode code = MiningResultCode::rejected;
    switch (result.error) {
        case MiningSubmitError::none: code = MiningResultCode::accepted; break;
        case MiningSubmitError::no_active_job:
        case MiningSubmitError::stale_job: code = MiningResultCode::stale_job; break;
        case MiningSubmitError::invalid_proof:
            code = MiningResultCode::invalid_proof;
            break;
        case MiningSubmitError::not_open:
        case MiningSubmitError::node_rejected: break;
    }
    return {job_id, code, result.block_identifier};
}

} // namespace onuros
