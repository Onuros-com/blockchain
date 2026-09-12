#pragma once

#include "onuros/local_node.hpp"
#include "onuros/persistent_shielded_state.hpp"
#include "onuros/private_block.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

namespace onuros {

enum class PrivateCommitJournalError {
    none,
    not_found,
    io_error,
    too_large,
    corrupt
};

struct PrivateCommitIntent {
    Block block;
    std::uint64_t adjusted_time = 0U;
};

struct PrivateCommitJournalResult {
    PrivateCommitJournalError error = PrivateCommitJournalError::none;
    std::optional<PrivateCommitIntent> intent;
};

class PrivateCommitJournal {
    static constexpr std::array<std::uint8_t, 8> magic_ =
        {'O', 'N', 'U', 'R', 'C', 'M', '0', '1'};
    static constexpr std::size_t framing_bytes_ = 8U + 8U + 4U + 32U;

    std::filesystem::path path_;
    DecodeLimits decode_limits_;
    std::size_t max_bytes_;

public:
    PrivateCommitJournal(std::filesystem::path path, DecodeLimits decode_limits,
                         std::size_t max_bytes)
        : path_(std::move(path)), decode_limits_(decode_limits),
          max_bytes_(max_bytes) {}

    const std::filesystem::path& path() const { return path_; }

    PrivateCommitJournalError write(const Block& block,
                                    std::uint64_t adjusted_time) const {
        try {
            const auto encoded = encode_block(block);
            if (encoded.size() > effective_block_limit(
                                     decode_limits_.max_block_bytes) ||
                encoded.size() > std::numeric_limits<std::uint32_t>::max() ||
                encoded.size() > max_bytes_ ||
                framing_bytes_ > max_bytes_ - encoded.size())
                return PrivateCommitJournalError::too_large;
            std::vector<std::uint8_t> bytes(magic_.begin(), magic_.end());
            detail::append_u64(bytes, adjusted_time);
            detail::append_u32(bytes,
                               static_cast<std::uint32_t>(encoded.size()));
            bytes.insert(bytes.end(), encoded.begin(), encoded.end());
            detail::append_hash(bytes, double_sha256(bytes));
            return detail::create_atomic(path_, bytes)
                ? PrivateCommitJournalError::none
                : PrivateCommitJournalError::io_error;
        } catch (const std::length_error&) {
            return PrivateCommitJournalError::too_large;
        }
    }

    PrivateCommitJournalResult read() const {
        std::error_code error;
        if (!std::filesystem::exists(path_, error))
            return {error ? PrivateCommitJournalError::io_error
                          : PrivateCommitJournalError::not_found,
                    std::nullopt};
        const auto size = std::filesystem::file_size(path_, error);
        if (error) return {PrivateCommitJournalError::io_error, std::nullopt};
        if (size > max_bytes_ ||
            size > static_cast<std::uintmax_t>(
                       std::numeric_limits<std::size_t>::max()))
            return {PrivateCommitJournalError::too_large, std::nullopt};
        std::ifstream input(path_, std::ios::binary);
        if (!input) return {PrivateCommitJournalError::io_error, std::nullopt};
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input && !bytes.empty())
            return {PrivateCommitJournalError::io_error, std::nullopt};
        if (bytes.size() < framing_bytes_ ||
            !std::equal(magic_.begin(), magic_.end(), bytes.begin()))
            return {PrivateCommitJournalError::corrupt, std::nullopt};

        const auto checksum_offset = bytes.size() - Hash256{}.size();
        const auto checksum = double_sha256(std::vector<std::uint8_t>(
            bytes.begin(),
            bytes.begin() + static_cast<std::ptrdiff_t>(checksum_offset)));
        if (!std::equal(checksum.begin(), checksum.end(),
                        bytes.begin() +
                            static_cast<std::ptrdiff_t>(checksum_offset)))
            return {PrivateCommitJournalError::corrupt, std::nullopt};

        std::size_t offset = magic_.size();
        std::uint64_t adjusted_time = 0U;
        std::uint32_t encoded_size = 0U;
        if (!detail::take_u64(bytes, offset, adjusted_time) ||
            !detail::take_u32(bytes, offset, encoded_size) ||
            encoded_size > effective_block_limit(
                               decode_limits_.max_block_bytes) ||
            offset > checksum_offset ||
            encoded_size != checksum_offset - offset)
            return {PrivateCommitJournalError::corrupt, std::nullopt};
        const std::vector<std::uint8_t> encoded(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(checksum_offset));
        auto block = decode_block(encoded, decode_limits_);
        if (!block)
            return {PrivateCommitJournalError::corrupt, std::nullopt};
        return {PrivateCommitJournalError::none,
                PrivateCommitIntent{std::move(*block), adjusted_time}};
    }

    PrivateCommitJournalError clear() const {
        std::error_code error;
        const bool removed = std::filesystem::remove(path_, error);
        if (error) return PrivateCommitJournalError::io_error;
        if (!removed) return PrivateCommitJournalError::none;
        return detail::sync_parent_directory(path_)
            ? PrivateCommitJournalError::none
            : PrivateCommitJournalError::io_error;
    }
};

enum class PrivateCommitError {
    none,
    recovery_required,
    unsupported_genesis,
    state_mismatch,
    private_validation_failed,
    journal_error,
    node_rejected,
    shielded_state_rejected
};

enum class PrivateRecoveryAction {
    none,
    discarded_uncommitted_intent,
    completed_shielded_commit,
    cleared_completed_intent
};

struct PrivateCommitResult {
    PrivateCommitError error = PrivateCommitError::none;
    PrivateRecoveryAction recovery_action = PrivateRecoveryAction::none;
    PrivateCommitJournalError journal_error = PrivateCommitJournalError::none;
    PrivateBlockError private_error = PrivateBlockError::none;
    PrivateAdmissionError admission_error = PrivateAdmissionError::none;
    PrivateProofError proof_error = PrivateProofError::none;
    PrivateRewardError reward_error = PrivateRewardError::none;
    LocalNodeResult node_result{};
    ShieldedStoreError shielded_error = ShieldedStoreError::none;

    bool accepted() const { return error == PrivateCommitError::none; }
};

class PrivateNodeCommitCoordinator {
    LocalNode& node_;
    PersistentShieldedState& shielded_;
    const PrivateTransactionVerifier& verifier_;
    const ShieldedRootCalculator& root_calculator_;
    const PrivateRewardPolicy& reward_policy_;
    PrivateAdmissionLimits admission_limits_;
    PrivateCommitJournal journal_;

    PrivateCommitResult validate_private(const Block& block,
            PreparedPrivateBlock& prepared) const {
        prepared = PrivateBlockValidator::prepare(
            shielded_.state(), block, verifier_, root_calculator_,
            reward_policy_, admission_limits_);
        if (prepared.accepted()) return {};
        PrivateCommitResult result;
        result.error = PrivateCommitError::private_validation_failed;
        result.private_error = prepared.error;
        result.admission_error = prepared.admission_error;
        result.proof_error = prepared.proof_error;
        result.reward_error = prepared.reward_error;
        return result;
    }

    PrivateCommitResult clear_with_action(PrivateRecoveryAction action) const {
        const auto journal_error = journal_.clear();
        if (journal_error != PrivateCommitJournalError::none) {
            PrivateCommitResult result;
            result.error = PrivateCommitError::journal_error;
            result.journal_error = journal_error;
            return result;
        }
        PrivateCommitResult result;
        result.recovery_action = action;
        return result;
    }

public:
    PrivateNodeCommitCoordinator(
            LocalNode& node, PersistentShieldedState& shielded,
            const PrivateTransactionVerifier& verifier,
            const ShieldedRootCalculator& root_calculator,
            const PrivateRewardPolicy& reward_policy,
            PrivateAdmissionLimits admission_limits,
            std::filesystem::path journal_path, DecodeLimits decode_limits,
            std::size_t max_journal_bytes)
        : node_(node), shielded_(shielded), verifier_(verifier),
          root_calculator_(root_calculator), reward_policy_(reward_policy),
          admission_limits_(admission_limits),
          journal_(std::move(journal_path), decode_limits,
                   max_journal_bytes) {}

    PrivateCommitResult submit(const Block& block,
                               std::uint64_t adjusted_time) {
        const auto pending = journal_.read();
        if (pending.error != PrivateCommitJournalError::not_found) {
            PrivateCommitResult result;
            result.error = pending.error == PrivateCommitJournalError::none
                ? PrivateCommitError::recovery_required
                : PrivateCommitError::journal_error;
            result.journal_error = pending.error;
            return result;
        }
        if (block.header.height == 0U)
            return {PrivateCommitError::unsupported_genesis};
        if (node_.active_state().tip() != block.header.previous ||
            shielded_.state().tip() != block.header.previous ||
            node_.active_state().tip() != shielded_.state().tip())
            return {PrivateCommitError::state_mismatch};

        PreparedPrivateBlock prepared;
        const auto validation = validate_private(block, prepared);
        if (!validation.accepted()) return validation;
        const auto journal_error = journal_.write(block, adjusted_time);
        if (journal_error != PrivateCommitJournalError::none) {
            PrivateCommitResult result;
            result.error = PrivateCommitError::journal_error;
            result.journal_error = journal_error;
            return result;
        }

        const auto node_result = node_.submit(block, adjusted_time);
        if (node_result.error != LocalNodeError::none) {
            auto result = clear_with_action(PrivateRecoveryAction::none);
            if (!result.accepted()) return result;
            result.error = PrivateCommitError::node_rejected;
            result.node_result = node_result;
            return result;
        }
        const auto id = block_id(block.header);
        if (node_.active_state().tip() != id) {
            PrivateCommitResult result;
            result.error = PrivateCommitError::state_mismatch;
            return result;
        }
        const auto shielded_error = shielded_.connect(
            id, block.header.shielded_root, *prepared.prepared);
        if (shielded_error != ShieldedStoreError::none) {
            PrivateCommitResult result;
            result.error = PrivateCommitError::shielded_state_rejected;
            result.shielded_error = shielded_error;
            return result;
        }
        return clear_with_action(PrivateRecoveryAction::none);
    }

    PrivateCommitResult recover() {
        const auto pending = journal_.read();
        if (pending.error == PrivateCommitJournalError::not_found) return {};
        if (pending.error != PrivateCommitJournalError::none || !pending.intent) {
            PrivateCommitResult result;
            result.error = PrivateCommitError::journal_error;
            result.journal_error = pending.error;
            return result;
        }
        const auto& block = pending.intent->block;
        if (block.header.height == 0U)
            return {PrivateCommitError::unsupported_genesis};
        const auto id = block_id(block.header);
        const auto* stored = node_.store().find(id);
        if (stored == nullptr)
            return clear_with_action(
                PrivateRecoveryAction::discarded_uncommitted_intent);
        if (encode_block(stored->block) != encode_block(block) ||
            node_.active_state().tip() != id)
            return {PrivateCommitError::state_mismatch};
        if (shielded_.state().tip() == id) {
            if (shielded_.state().root() != block.header.shielded_root)
                return {PrivateCommitError::state_mismatch};
            return clear_with_action(
                PrivateRecoveryAction::cleared_completed_intent);
        }
        if (shielded_.state().tip() != block.header.previous)
            return {PrivateCommitError::state_mismatch};

        PreparedPrivateBlock prepared;
        const auto validation = validate_private(block, prepared);
        if (!validation.accepted()) return validation;
        const auto shielded_error = shielded_.connect(
            id, block.header.shielded_root, *prepared.prepared);
        if (shielded_error != ShieldedStoreError::none) {
            PrivateCommitResult result;
            result.error = PrivateCommitError::shielded_state_rejected;
            result.shielded_error = shielded_error;
            return result;
        }
        return clear_with_action(
            PrivateRecoveryAction::completed_shielded_commit);
    }

    const PrivateCommitJournal& journal() const { return journal_; }
};

} // namespace onuros
