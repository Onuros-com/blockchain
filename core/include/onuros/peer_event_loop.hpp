#pragma once

#include "onuros/p2p_transport.hpp"
#include "onuros/peer_manager.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace onuros {

using EventPeerId = std::uint64_t;

struct PeerEventLoopLimits {
    std::size_t maximum_peers = 64U;
    std::size_t maximum_receive_buffer_bytes = 2U * 1024U * 1024U;
    std::size_t maximum_read_bytes_per_tick = 64U * 1024U;
    std::size_t maximum_write_bytes_per_tick = 64U * 1024U;
    std::size_t maximum_frames_per_tick = 64U;
    PeerResourceLimits resources;
    PeerManagerLimits deadlines;
};

enum class PeerLoopError {
    none,
    peer_limit,
    duplicate_peer,
    queue_limit,
    unknown_peer
};

enum class PeerFrameAction {
    keep,
    disconnect
};

struct PeerEventLoopStats {
    std::uint64_t received_bytes = 0U;
    std::uint64_t sent_bytes = 0U;
    std::uint64_t received_frames = 0U;
    std::uint64_t disconnected_peers = 0U;
    std::uint64_t protocol_failures = 0U;
    std::uint64_t timed_out_peers = 0U;
    std::uint64_t policy_disconnects = 0U;
};

class PeerEventLoop {
    struct PeerState {
        std::unique_ptr<PeerTransport> transport;
        FrameStreamDecoder decoder;
        PeerSession session;
        PeerDeadlineTracker deadline;
        PeerResourceGuard resources;
        std::deque<std::vector<std::uint8_t>> send_queue;
        std::size_t send_offset = 0U;

        PeerState(std::unique_ptr<PeerTransport> peer_transport,
                  const PeerEventLoopLimits& limits,
                  HandshakePolicy policy, std::uint64_t now)
            : transport(std::move(peer_transport)),
              decoder({}, limits.maximum_receive_buffer_bytes),
              session(std::move(policy)), deadline(limits.deadlines, now),
              resources(limits.resources) {}
    };

    PeerEventLoopLimits limits_;
    std::map<EventPeerId, PeerState> peers_;
    PeerEventLoopStats stats_;

    void disconnect(std::map<EventPeerId, PeerState>::iterator& peer) {
        ++stats_.disconnected_peers;
        peer = peers_.erase(peer);
    }

public:
    explicit PeerEventLoop(PeerEventLoopLimits limits)
        : limits_(std::move(limits)) {}

    PeerLoopError add_peer(EventPeerId identifier,
                           std::unique_ptr<PeerTransport> transport,
                           HandshakePolicy policy, std::uint64_t now) {
        if (!transport || peers_.size() >= limits_.maximum_peers)
            return PeerLoopError::peer_limit;
        if (peers_.find(identifier) != peers_.end())
            return PeerLoopError::duplicate_peer;
        peers_.emplace(std::piecewise_construct,
            std::forward_as_tuple(identifier),
            std::forward_as_tuple(std::move(transport), limits_,
                                  std::move(policy), now));
        return PeerLoopError::none;
    }

    PeerLoopError queue(EventPeerId identifier, const P2pFrame& frame) {
        const auto found = peers_.find(identifier);
        if (found == peers_.end()) return PeerLoopError::unknown_peer;
        auto encoded = encode_p2p_frame(frame);
        if (!found->second.resources.reserve_bytes(encoded.size()))
            return PeerLoopError::queue_limit;
        found->second.send_queue.push_back(std::move(encoded));
        return PeerLoopError::none;
    }

    template <typename FrameHandler>
    void tick_impl(std::uint64_t now, FrameHandler&& on_frame) {
        std::array<std::uint8_t, 16U * 1024U> receive_buffer{};
        for (auto peer = peers_.begin(); peer != peers_.end();) {
            auto& state = peer->second;
            if (state.deadline.timeout(now) != PeerTimeout::none) {
                ++stats_.timed_out_peers;
                disconnect(peer);
                continue;
            }

            std::size_t written_this_tick = 0U;
            bool closed = false;
            while (!state.send_queue.empty() &&
                   written_this_tick < limits_.maximum_write_bytes_per_tick) {
                auto& bytes = state.send_queue.front();
                const auto remaining = bytes.size() - state.send_offset;
                const auto budget = limits_.maximum_write_bytes_per_tick -
                                    written_this_tick;
                const auto result = state.transport->send_some(
                    bytes.data() + state.send_offset, std::min(remaining, budget));
                if (result.status == SocketIoStatus::would_block) break;
                if (result.status != SocketIoStatus::ok || result.bytes == 0U) {
                    closed = true;
                    break;
                }
                state.send_offset += result.bytes;
                written_this_tick += result.bytes;
                stats_.sent_bytes += result.bytes;
                state.deadline.mark_activity(now);
                if (state.send_offset == bytes.size()) {
                    state.resources.release_bytes(bytes.size());
                    state.send_queue.pop_front();
                    state.send_offset = 0U;
                }
            }
            if (closed) {
                disconnect(peer);
                continue;
            }

            std::size_t read_this_tick = 0U;
            while (read_this_tick < limits_.maximum_read_bytes_per_tick) {
                const auto budget = std::min(receive_buffer.size(),
                    limits_.maximum_read_bytes_per_tick - read_this_tick);
                const auto result = state.transport->receive_some(
                    receive_buffer.data(), budget);
                if (result.status == SocketIoStatus::would_block) break;
                if (result.status != SocketIoStatus::ok || result.bytes == 0U ||
                    !state.decoder.feed(receive_buffer.data(), result.bytes)) {
                    closed = true;
                    break;
                }
                read_this_tick += result.bytes;
                stats_.received_bytes += result.bytes;
                state.deadline.mark_activity(now);
            }
            if (closed) {
                disconnect(peer);
                continue;
            }

            std::size_t frames = 0U;
            while (frames < limits_.maximum_frames_per_tick) {
                const auto parsed = state.decoder.next();
                if (parsed.status == FrameStreamStatus::incomplete) break;
                if (parsed.status == FrameStreamStatus::failed) {
                    ++stats_.protocol_failures;
                    closed = true;
                    break;
                }
                const auto was_established =
                    state.session.state() == PeerSessionState::established;
                if (state.session.receive(parsed.frame,
                        state.transport->authenticated()) != PeerSessionError::none) {
                    ++stats_.protocol_failures;
                    closed = true;
                    break;
                }
                if (!was_established &&
                    state.session.state() == PeerSessionState::established)
                    state.deadline.mark_handshake_complete(now);
                ++frames;
                ++stats_.received_frames;
                if (on_frame(peer->first, parsed.frame)) {
                    ++stats_.policy_disconnects;
                    closed = true;
                    break;
                }
            }
            if (closed) {
                disconnect(peer);
                continue;
            }
            ++peer;
        }
    }

    void tick(std::uint64_t now,
              const std::function<void(EventPeerId, const P2pFrame&)>& on_frame) {
        tick_impl(now, [&on_frame](EventPeerId peer, const P2pFrame& frame) {
            if (on_frame) on_frame(peer, frame);
            return false;
        });
    }

    void tick_with_policy(
            std::uint64_t now,
            const std::function<PeerFrameAction(
                EventPeerId, const P2pFrame&)>& on_frame) {
        tick_impl(now, [&on_frame](EventPeerId peer, const P2pFrame& frame) {
            return on_frame &&
                   on_frame(peer, frame) == PeerFrameAction::disconnect;
        });
    }

    void shutdown() noexcept { peers_.clear(); }
    std::size_t peer_count() const noexcept { return peers_.size(); }
    const PeerEventLoopStats& stats() const noexcept { return stats_; }
};

} // namespace onuros
