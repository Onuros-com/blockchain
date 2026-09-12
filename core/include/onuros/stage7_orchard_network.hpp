#pragma once

#include "onuros/orchard_ffi_backend.hpp"
#include "onuros/peer_event_loop.hpp"
#include "onuros/stage7_node_admission.hpp"

#include <cstddef>
#include <functional>
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

} // namespace onuros
