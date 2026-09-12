#pragma once

#include "onuros/orchard_ffi_backend.hpp"
#include "onuros/peer_event_loop.hpp"
#include "onuros/stage7_node_admission.hpp"

#include <cstddef>
#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <utility>

namespace onuros {

// Production ownership boundary for private transaction relay.
//
// When ONUROS_ORCHARD_FFI_ENABLED is configured, the default constructor path
// binds CanonicalPrivateTransactionVerifier to the real Orchard FFI verifier.
// The injected-verifier constructor exists for deterministic boundary tests.
class OrchardNetworkAdmission {
public:
    using FrameObserver = std::function<void(
        EventPeerId, const NetworkFrameAdmissionResult&)>;

private:
    OrchardFfiBackend backend_;
    CanonicalPrivateTransactionVerifier verifier_;
    PrivateMempool mempool_;
    ValidatedRelayPool relay_pool_;
    Stage7NodeAdmission admission_;

public:
    OrchardNetworkAdmission(
            const ShieldedState& state,
            PrivateBundleLimits bundle_limits,
            PrivateMempoolLimits mempool_limits,
            std::size_t maximum_relay_transactions,
            std::size_t maximum_relay_bytes,
            Stage7NodeAdmission::BlockAdmission block_admission = {},
            NetworkTransactionBatchLimits batch_limits = {})
        : backend_(), verifier_(backend_, bundle_limits),
          mempool_(mempool_limits),
          relay_pool_(maximum_relay_transactions, maximum_relay_bytes),
          admission_(state, verifier_, mempool_, relay_pool_,
                     std::move(block_admission), batch_limits) {}

    OrchardNetworkAdmission(
            OrchardVerifyFunction verifier_function,
            const ShieldedState& state,
            PrivateBundleLimits bundle_limits,
            PrivateMempoolLimits mempool_limits,
            std::size_t maximum_relay_transactions,
            std::size_t maximum_relay_bytes,
            Stage7NodeAdmission::BlockAdmission block_admission = {},
            NetworkTransactionBatchLimits batch_limits = {})
        : backend_(verifier_function), verifier_(backend_, bundle_limits),
          mempool_(mempool_limits),
          relay_pool_(maximum_relay_transactions, maximum_relay_bytes),
          admission_(state, verifier_, mempool_, relay_pool_,
                     std::move(block_admission), batch_limits) {}

    std::optional<NetworkFrameAdmissionResult> handle_frame(
            EventPeerId, const P2pFrame& frame) {
        if (frame.type != P2pMessageType::transactions)
            return std::nullopt;
        return admission_.admit_frame(frame);
    }

    std::function<void(EventPeerId, const P2pFrame&)> make_frame_handler(
            FrameObserver observer = {}) {
        return [this, observer = std::move(observer)](
                       EventPeerId peer, const P2pFrame& frame) {
            const auto result = handle_frame(peer, frame);
            if (result && observer) observer(peer, *result);
        };
    }

    const PrivateMempool& mempool() const noexcept { return mempool_; }
    const ValidatedRelayPool& relay_pool() const noexcept {
        return relay_pool_;
    }
    const NetworkAdmissionMetrics& metrics() const noexcept {
        return admission_.metrics();
    }
};

struct OrchardPeerPolicyLimits {
    std::size_t malformed_payload_score = 100U;
    std::size_t invalid_proof_score = 25U;
    std::size_t conflicting_transaction_score = 5U;
    std::size_t disconnect_score = 100U;
};

// Maps transaction-admission failures to peer actions. Local relay exhaustion is
// backpressure, not peer misconduct, and therefore carries no penalty.
class OrchardPeerAdmissionPolicy {
    OrchardNetworkAdmission& admission_;
    OrchardPeerPolicyLimits limits_;
    std::map<EventPeerId, std::size_t> scores_;

    std::size_t penalty(const NetworkFrameAdmissionResult& result) const {
        if (result.error == NetworkFrameAdmissionError::invalid_payload)
            return limits_.malformed_payload_score;
        if (result.error != NetworkFrameAdmissionError::transaction_rejected)
            return 0U;
        if (result.transaction_result.error ==
                NetworkTransactionAdmissionError::relay_rejected)
            return 0U;
        const auto& mempool = result.transaction_result.mempool_result;
        if (mempool.error == PrivateMempoolError::verification_failed)
            return limits_.invalid_proof_score;
        return limits_.conflicting_transaction_score;
    }

public:
    OrchardPeerAdmissionPolicy(
            OrchardNetworkAdmission& admission,
            OrchardPeerPolicyLimits limits = {})
        : admission_(admission), limits_(limits) {}

    PeerFrameAction handle(EventPeerId peer, const P2pFrame& frame) {
        const auto result = admission_.handle_frame(peer, frame);
        if (!result || result->accepted()) return PeerFrameAction::keep;
        const auto added = penalty(*result);
        if (added == 0U) return PeerFrameAction::keep;
        auto& score = scores_[peer];
        score = added > std::numeric_limits<std::size_t>::max() - score
            ? std::numeric_limits<std::size_t>::max()
            : score + added;
        return score >= limits_.disconnect_score
            ? PeerFrameAction::disconnect : PeerFrameAction::keep;
    }

    std::function<PeerFrameAction(EventPeerId, const P2pFrame&)>
    make_handler() {
        return [this](EventPeerId peer, const P2pFrame& frame) {
            return handle(peer, frame);
        };
    }

    std::size_t score(EventPeerId peer) const noexcept {
        const auto found = scores_.find(peer);
        return found == scores_.end() ? 0U : found->second;
    }

    void forget(EventPeerId peer) { scores_.erase(peer); }
};

} // namespace onuros
