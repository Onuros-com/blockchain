#pragma once

#include "onuros/onuros_privacy_engine_backend.hpp"
#include "onuros/peer_event_loop.hpp"
#include "onuros/stage7_node_admission.hpp"

#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace onuros {

// Active network-admission composition for Onuros Shielded Payment v1.
// The payment verifier is Groth16/Poseidon through the versioned Privacy
// Engine ABI. No Orchard/Halo2 backend is reachable from this class.
class PrivacyEngineNetworkAdmission {
public:
    using FrameObserver = std::function<void(
        EventPeerId, const NetworkFrameAdmissionResult&)>;

private:
    OnurosPrivacyEngineBackend backend_;
    PrivateMempool mempool_;
    ValidatedRelayPool relay_pool_;
    Stage7NodeAdmission admission_;

public:
    PrivacyEngineNetworkAdmission(
            const onuros_privacy_engine_v1* engine,
            OnurosPrivacyEngineBackend::ValidatePayment validate,
            std::vector<onuros_accepted_root_v1> roots,
            std::uint32_t chain_height, std::uint32_t max_root_age,
            const ShieldedState& state,
            PrivateMempoolLimits mempool_limits,
            std::size_t maximum_relay_transactions,
            std::size_t maximum_relay_bytes,
            Stage7NodeAdmission::BlockAdmission block_admission = {},
            NetworkTransactionBatchLimits batch_limits = {})
        : backend_(engine, validate, std::move(roots), chain_height,
                   max_root_age),
          mempool_(mempool_limits),
          relay_pool_(maximum_relay_transactions, maximum_relay_bytes),
          admission_(state, backend_, mempool_, relay_pool_,
                     std::move(block_admission), batch_limits) {}

#ifdef ONUROS_PRIVACY_ENGINE_ENABLED
    PrivacyEngineNetworkAdmission(
            const onuros_privacy_engine_v1* engine,
            std::vector<onuros_accepted_root_v1> roots,
            std::uint32_t chain_height, std::uint32_t max_root_age,
            const ShieldedState& state,
            PrivateMempoolLimits mempool_limits,
            std::size_t maximum_relay_transactions,
            std::size_t maximum_relay_bytes,
            Stage7NodeAdmission::BlockAdmission block_admission = {},
            NetworkTransactionBatchLimits batch_limits = {})
        : PrivacyEngineNetworkAdmission(
              engine,
              &onuros_privacy_engine_validate_payment_with_roots_at_height_v1,
              std::move(roots), chain_height, max_root_age, state,
              mempool_limits, maximum_relay_transactions,
              maximum_relay_bytes, std::move(block_admission), batch_limits) {}
#endif

    std::optional<NetworkFrameAdmissionResult> handle_frame(
            EventPeerId, const P2pFrame& frame) {
        if (frame.type != P2pMessageType::transactions)
            return std::nullopt;
        return admission_.admit_frame(frame);
    }

    std::optional<NetworkFrameAdmissionResult> handle_frame_parallel(
            const P2pFrame& frame, std::size_t workers) {
        if (frame.type != P2pMessageType::transactions)
            return std::nullopt;
        return admission_.admit_frame_parallel(frame, workers);
    }

    const PrivateMempool& mempool() const noexcept { return mempool_; }
    const ValidatedRelayPool& relay_pool() const noexcept { return relay_pool_; }
    const NetworkAdmissionMetrics& metrics() const noexcept {
        return admission_.metrics();
    }
};

struct PrivacyEnginePeerPolicyLimits {
    std::size_t malformed_payload_score = 100U;
    std::size_t invalid_proof_score = 25U;
    std::size_t conflicting_transaction_score = 5U;
    std::size_t disconnect_score = 100U;
};

class PrivacyEnginePeerAdmissionPolicy {
    PrivacyEngineNetworkAdmission& admission_;
    PrivacyEnginePeerPolicyLimits limits_;
    std::map<EventPeerId, std::size_t> scores_;

    std::size_t penalty(const NetworkFrameAdmissionResult& result) const {
        if (result.error == NetworkFrameAdmissionError::invalid_payload)
            return limits_.malformed_payload_score;
        if (result.error != NetworkFrameAdmissionError::transaction_rejected)
            return 0U;
        if (result.transaction_result.error ==
                NetworkTransactionAdmissionError::relay_rejected)
            return 0U;
        if (result.transaction_result.mempool_result.error ==
                PrivateMempoolError::verification_failed)
            return limits_.invalid_proof_score;
        return limits_.conflicting_transaction_score;
    }

public:
    explicit PrivacyEnginePeerAdmissionPolicy(
            PrivacyEngineNetworkAdmission& admission,
            PrivacyEnginePeerPolicyLimits limits = {})
        : admission_(admission), limits_(limits) {}

    PeerFrameAction handle(EventPeerId peer, const P2pFrame& frame) {
        const auto result = admission_.handle_frame(peer, frame);
        if (!result || result->accepted()) return PeerFrameAction::keep;
        const auto added = penalty(*result);
        if (added == 0U) return PeerFrameAction::keep;
        auto& score = scores_[peer];
        score = added > std::numeric_limits<std::size_t>::max() - score
            ? std::numeric_limits<std::size_t>::max() : score + added;
        return score >= limits_.disconnect_score
            ? PeerFrameAction::disconnect : PeerFrameAction::keep;
    }

    std::size_t score(EventPeerId peer) const noexcept {
        const auto found = scores_.find(peer);
        return found == scores_.end() ? 0U : found->second;
    }

    void forget(EventPeerId peer) { scores_.erase(peer); }
};

} // namespace onuros
