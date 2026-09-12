#pragma once

#include "onuros/p2p_protocol.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace onuros {

#ifdef _WIN32
using NativeSocket = SOCKET;
using NativeSocketLength = int;
inline constexpr NativeSocket invalid_socket = INVALID_SOCKET;
#else
using NativeSocket = int;
using NativeSocketLength = socklen_t;
inline constexpr NativeSocket invalid_socket = -1;
#endif

inline void close_native_socket(NativeSocket socket) noexcept {
    if (socket == invalid_socket) return;
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}

inline bool socket_would_block() noexcept {
#ifdef _WIN32
    const auto error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS;
#endif
}

inline bool set_socket_nonblocking(NativeSocket socket) noexcept {
#ifdef _WIN32
    u_long enabled = 1U;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    const auto flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

class SocketRuntime {
    bool ready_ = true;
public:
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#endif
    }
    ~SocketRuntime() {
#ifdef _WIN32
        if (ready_) WSACleanup();
#endif
    }
    SocketRuntime(const SocketRuntime&) = delete;
    SocketRuntime& operator=(const SocketRuntime&) = delete;
    bool ready() const noexcept { return ready_; }
};

enum class SocketIoStatus { ok, would_block, closed, error };
struct SocketIoResult {
    SocketIoStatus status = SocketIoStatus::error;
    std::size_t bytes = 0U;
};

class TcpConnection {
    NativeSocket socket_ = invalid_socket;
public:
    TcpConnection() = default;
    explicit TcpConnection(NativeSocket socket) : socket_(socket) {}
    ~TcpConnection() { close_native_socket(socket_); }
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    TcpConnection(TcpConnection&& other) noexcept : socket_(other.socket_) {
        other.socket_ = invalid_socket;
    }
    TcpConnection& operator=(TcpConnection&& other) noexcept {
        if (this == &other) return *this;
        close_native_socket(socket_);
        socket_ = other.socket_;
        other.socket_ = invalid_socket;
        return *this;
    }

    static std::optional<TcpConnection> connect_ipv4(
            const std::string& address, std::uint16_t port) {
        const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == invalid_socket) return std::nullopt;
        sockaddr_in endpoint{};
        endpoint.sin_family = AF_INET;
        endpoint.sin_port = htons(port);
        if (inet_pton(AF_INET, address.c_str(), &endpoint.sin_addr) != 1 ||
            ::connect(socket, reinterpret_cast<const sockaddr*>(&endpoint),
                      static_cast<NativeSocketLength>(sizeof(endpoint))) != 0 ||
            !set_socket_nonblocking(socket)) {
            close_native_socket(socket);
            return std::nullopt;
        }
        return TcpConnection(socket);
    }

    bool valid() const noexcept { return socket_ != invalid_socket; }
    NativeSocket native_socket() const noexcept { return socket_; }
    SocketIoResult send_some(const std::uint8_t* data, std::size_t size) {
        if (!valid()) return {SocketIoStatus::error, 0U};
        const auto bounded = std::min<std::size_t>(
            size, static_cast<std::size_t>(std::numeric_limits<int>::max()));
#ifdef _WIN32
        const auto sent = ::send(socket_, reinterpret_cast<const char*>(data),
                                 static_cast<int>(bounded), 0);
#else
        const auto sent = ::send(socket_, data, bounded, 0);
#endif
        if (sent > 0) return {SocketIoStatus::ok, static_cast<std::size_t>(sent)};
        if (sent == 0) return {SocketIoStatus::closed, 0U};
        if (socket_would_block()) return {SocketIoStatus::would_block, 0U};
        return {SocketIoStatus::error, 0U};
    }
    SocketIoResult receive_some(std::uint8_t* data, std::size_t size) {
        if (!valid()) return {SocketIoStatus::error, 0U};
        const auto bounded = std::min<std::size_t>(
            size, static_cast<std::size_t>(std::numeric_limits<int>::max()));
#ifdef _WIN32
        const auto received = ::recv(socket_, reinterpret_cast<char*>(data),
                                     static_cast<int>(bounded), 0);
#else
        const auto received = ::recv(socket_, data, bounded, 0);
#endif
        if (received > 0)
            return {SocketIoStatus::ok, static_cast<std::size_t>(received)};
        if (received == 0) return {SocketIoStatus::closed, 0U};
        if (socket_would_block()) return {SocketIoStatus::would_block, 0U};
        return {SocketIoStatus::error, 0U};
    }
};

class PeerTransport {
public:
    virtual ~PeerTransport() = default;
    virtual SocketIoResult send_some(const std::uint8_t* data,
                                     std::size_t size) = 0;
    virtual SocketIoResult receive_some(std::uint8_t* data,
                                        std::size_t size) = 0;
    virtual bool authenticated() const noexcept = 0;
};

class TcpPeerTransport final : public PeerTransport {
    TcpConnection connection_;
public:
    explicit TcpPeerTransport(TcpConnection connection)
        : connection_(std::move(connection)) {}
    SocketIoResult send_some(const std::uint8_t* data,
                             std::size_t size) override {
        return connection_.send_some(data, size);
    }
    SocketIoResult receive_some(std::uint8_t* data,
                                std::size_t size) override {
        return connection_.receive_some(data, size);
    }
    bool authenticated() const noexcept override { return false; }
};

class TcpListener {
    NativeSocket socket_ = invalid_socket;
    std::uint16_t port_ = 0U;
public:
    TcpListener() = default;
    ~TcpListener() { close_native_socket(socket_); }
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;
    TcpListener(TcpListener&& other) noexcept
        : socket_(other.socket_), port_(other.port_) {
        other.socket_ = invalid_socket;
        other.port_ = 0U;
    }
    TcpListener& operator=(TcpListener&& other) noexcept {
        if (this == &other) return *this;
        close_native_socket(socket_);
        socket_ = other.socket_;
        port_ = other.port_;
        other.socket_ = invalid_socket;
        other.port_ = 0U;
        return *this;
    }

    static std::optional<TcpListener> listen_loopback(
            std::uint16_t requested_port = 0U, int backlog = 16) {
        const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == invalid_socket) return std::nullopt;
        int reuse = 1;
#ifdef _WIN32
        (void)setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
                         reinterpret_cast<const char*>(&reuse),
                         static_cast<int>(sizeof(reuse)));
#else
        (void)setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
        sockaddr_in endpoint{};
        endpoint.sin_family = AF_INET;
        endpoint.sin_port = htons(requested_port);
        endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(socket, reinterpret_cast<const sockaddr*>(&endpoint),
                   static_cast<NativeSocketLength>(sizeof(endpoint))) != 0 ||
            ::listen(socket, backlog) != 0 ||
            !set_socket_nonblocking(socket)) {
            close_native_socket(socket);
            return std::nullopt;
        }
        sockaddr_in bound{};
        NativeSocketLength size =
            static_cast<NativeSocketLength>(sizeof(bound));
        if (getsockname(socket, reinterpret_cast<sockaddr*>(&bound), &size) != 0) {
            close_native_socket(socket);
            return std::nullopt;
        }
        TcpListener listener;
        listener.socket_ = socket;
        listener.port_ = ntohs(bound.sin_port);
        return listener;
    }

    std::optional<TcpConnection> accept_one() {
        sockaddr_in remote{};
        NativeSocketLength size =
            static_cast<NativeSocketLength>(sizeof(remote));
        const auto accepted = ::accept(socket_,
            reinterpret_cast<sockaddr*>(&remote), &size);
        if (accepted == invalid_socket) return std::nullopt;
        if (!set_socket_nonblocking(accepted)) {
            close_native_socket(accepted);
            return std::nullopt;
        }
        return TcpConnection(accepted);
    }
    bool valid() const noexcept { return socket_ != invalid_socket; }
    std::uint16_t port() const noexcept { return port_; }
};

enum class FrameStreamStatus { incomplete, frame_ready, failed };
struct FrameStreamResult {
    FrameStreamStatus status = FrameStreamStatus::incomplete;
    P2pFrameError error = P2pFrameError::none;
    P2pFrame frame;
};

class FrameStreamDecoder {
    P2pFrameLimits frame_limits_;
    std::size_t maximum_buffer_bytes_;
    std::vector<std::uint8_t> buffer_;
    bool failed_ = false;

    static std::uint16_t u16_at(const std::vector<std::uint8_t>& bytes,
                                std::size_t offset) {
        return static_cast<std::uint16_t>(bytes[offset]) |
            (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
    }
    static std::uint32_t u32_at(const std::vector<std::uint8_t>& bytes,
                                std::size_t offset) {
        std::uint32_t value = 0U;
        for (std::size_t i = 0U; i < 4U; ++i)
            value |= static_cast<std::uint32_t>(bytes[offset + i]) << (8U * i);
        return value;
    }

    FrameStreamResult fail(P2pFrameError error) {
        failed_ = true;
        buffer_.clear();
        return {FrameStreamStatus::failed, error, {}};
    }
public:
    FrameStreamDecoder(P2pFrameLimits limits, std::size_t maximum_buffer_bytes)
        : frame_limits_(limits), maximum_buffer_bytes_(maximum_buffer_bytes) {}
    bool feed(const std::uint8_t* data, std::size_t size) {
        if (failed_ || size > maximum_buffer_bytes_ -
                std::min(maximum_buffer_bytes_, buffer_.size())) {
            failed_ = true;
            return false;
        }
        buffer_.insert(buffer_.end(), data, data + size);
        return true;
    }
    FrameStreamResult next() {
        if (failed_) return {FrameStreamStatus::failed,
                             P2pFrameError::oversized_payload, {}};
        if (buffer_.size() < p2p_frame_header_size) return {};
        if (u32_at(buffer_, 0U) != frame_limits_.network_magic)
            return fail(P2pFrameError::wrong_network);
        const auto protocol = u16_at(buffer_, 4U);
        if (protocol < frame_limits_.minimum_protocol ||
            protocol > frame_limits_.maximum_protocol)
            return fail(P2pFrameError::incompatible_protocol);
        if (!known_message_type(u16_at(buffer_, 6U)))
            return fail(P2pFrameError::unknown_message);
        const auto payload_size = u32_at(buffer_, 16U);
        if (payload_size > frame_limits_.maximum_payload_bytes)
            return fail(P2pFrameError::oversized_payload);
        const auto frame_size = p2p_frame_header_size +
                                static_cast<std::size_t>(payload_size);
        if (buffer_.size() < frame_size) return {};
        const std::vector<std::uint8_t> encoded(
            buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
        const auto decoded = decode_p2p_frame(encoded, frame_limits_);
        if (decoded.error != P2pFrameError::none) return fail(decoded.error);
        buffer_.erase(buffer_.begin(),
                      buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size));
        return {FrameStreamStatus::frame_ready, P2pFrameError::none,
                decoded.frame};
    }
    std::size_t buffered_bytes() const noexcept { return buffer_.size(); }
    bool failed() const noexcept { return failed_; }
};

enum class PeerSessionState { awaiting_hello, established, closed };
enum class PeerSessionError {
    none, expected_hello, malformed_hello, handshake_rejected,
    duplicate_hello, closed
};

class PeerSession {
    HandshakePolicy policy_;
    PeerSessionState state_ = PeerSessionState::awaiting_hello;
    std::uint16_t negotiated_protocol_ = 0U;
public:
    explicit PeerSession(HandshakePolicy policy) : policy_(std::move(policy)) {}
    PeerSessionError receive(const P2pFrame& frame,
                             bool transport_authenticated) {
        if (state_ == PeerSessionState::closed) return PeerSessionError::closed;
        if (state_ == PeerSessionState::established) {
            if (frame.type == P2pMessageType::hello) {
                state_ = PeerSessionState::closed;
                return PeerSessionError::duplicate_hello;
            }
            return PeerSessionError::none;
        }
        if (frame.type != P2pMessageType::hello) {
            state_ = PeerSessionState::closed;
            return PeerSessionError::expected_hello;
        }
        const auto hello = decode_hello(frame.payload);
        if (!hello) {
            state_ = PeerSessionState::closed;
            return PeerSessionError::malformed_hello;
        }
        const auto result = validate_hello(*hello, policy_, transport_authenticated);
        if (result.error != HandshakeError::none) {
            state_ = PeerSessionState::closed;
            return PeerSessionError::handshake_rejected;
        }
        negotiated_protocol_ = result.negotiated_protocol;
        state_ = PeerSessionState::established;
        return PeerSessionError::none;
    }
    PeerSessionState state() const noexcept { return state_; }
    std::uint16_t negotiated_protocol() const noexcept {
        return negotiated_protocol_;
    }
};

struct PeerResourceLimits {
    std::size_t maximum_queued_bytes = 2U * 1024U * 1024U;
    std::size_t maximum_inflight_requests = 32U;
    std::size_t maximum_validation_jobs = 8U;
    std::uint32_t ban_score = 100U;
};

class PeerResourceGuard {
    PeerResourceLimits limits_;
    std::size_t queued_bytes_ = 0U;
    std::size_t validation_jobs_ = 0U;
    std::set<std::uint64_t> requests_;
    std::uint32_t score_ = 0U;
public:
    explicit PeerResourceGuard(PeerResourceLimits limits) : limits_(limits) {}
    bool reserve_bytes(std::size_t bytes) {
        if (bytes > limits_.maximum_queued_bytes -
                std::min(limits_.maximum_queued_bytes, queued_bytes_))
            return false;
        queued_bytes_ += bytes;
        return true;
    }
    void release_bytes(std::size_t bytes) noexcept {
        queued_bytes_ -= std::min(queued_bytes_, bytes);
    }
    bool begin_request(std::uint64_t request_id) {
        if (request_id == 0U || requests_.size() >= limits_.maximum_inflight_requests)
            return false;
        return requests_.insert(request_id).second;
    }
    void finish_request(std::uint64_t request_id) { requests_.erase(request_id); }
    bool begin_validation() {
        if (validation_jobs_ >= limits_.maximum_validation_jobs) return false;
        ++validation_jobs_;
        return true;
    }
    void finish_validation() noexcept {
        if (validation_jobs_ != 0U) --validation_jobs_;
    }
    void penalize(std::uint32_t points) noexcept {
        score_ = points > std::numeric_limits<std::uint32_t>::max() - score_
            ? std::numeric_limits<std::uint32_t>::max()
            : score_ + points;
    }
    bool banned() const noexcept { return score_ >= limits_.ban_score; }
    std::size_t queued_bytes() const noexcept { return queued_bytes_; }
    std::size_t inflight_requests() const noexcept { return requests_.size(); }
    std::uint32_t score() const noexcept { return score_; }
};

} // namespace onuros
