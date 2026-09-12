#pragma once

#include "onuros/p2p_transport.hpp"

#include <openssl/err.h>
#include <openssl/opensslv.h>
#include <openssl/ssl.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace onuros {

struct SslContextDeleter {
    void operator()(SSL_CTX* context) const noexcept { SSL_CTX_free(context); }
};
struct SslDeleter {
    void operator()(SSL* ssl) const noexcept { SSL_free(ssl); }
};

class TlsContext {
    std::unique_ptr<SSL_CTX, SslContextDeleter> context_;

    static std::optional<TlsContext> create(const std::string& certificate,
            const std::string& private_key, const std::string& trust_store) {
        std::unique_ptr<SSL_CTX, SslContextDeleter> context(
            SSL_CTX_new(TLS_method()));
        if (!context || SSL_CTX_set_min_proto_version(context.get(), TLS1_3_VERSION) != 1 ||
            SSL_CTX_set_max_proto_version(context.get(), TLS1_3_VERSION) != 1 ||
            SSL_CTX_load_verify_locations(context.get(), trust_store.c_str(), nullptr) != 1 ||
            SSL_CTX_use_certificate_chain_file(context.get(), certificate.c_str()) != 1 ||
            SSL_CTX_use_PrivateKey_file(context.get(), private_key.c_str(),
                                        SSL_FILETYPE_PEM) != 1 ||
            SSL_CTX_check_private_key(context.get()) != 1)
            return std::nullopt;
        SSL_CTX_set_verify(context.get(),
            SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
        SSL_CTX_set_verify_depth(context.get(), 4);
        SSL_CTX_set_options(context.get(), SSL_OP_NO_RENEGOTIATION |
                                           SSL_OP_NO_COMPRESSION);
        return TlsContext(std::move(context));
    }

    explicit TlsContext(std::unique_ptr<SSL_CTX, SslContextDeleter> context)
        : context_(std::move(context)) {}

public:
    TlsContext(TlsContext&&) noexcept = default;
    TlsContext& operator=(TlsContext&&) noexcept = default;
    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;

    static std::optional<TlsContext> mutual(const std::string& certificate,
            const std::string& private_key, const std::string& trust_store) {
        return create(certificate, private_key, trust_store);
    }
    SSL_CTX* native_context() const noexcept { return context_.get(); }
};

enum class TlsRole { client, server };
enum class TlsStatus { ok, would_block, closed, error };

struct TlsIoResult {
    TlsStatus status = TlsStatus::error;
    std::size_t bytes = 0U;
};

class TlsConnection {
    std::unique_ptr<SSL, SslDeleter> ssl_;
    bool authenticated_ = false;

    TlsStatus classify(int result) const noexcept {
        const auto error = SSL_get_error(ssl_.get(), result);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE)
            return TlsStatus::would_block;
        if (error == SSL_ERROR_ZERO_RETURN) return TlsStatus::closed;
        return TlsStatus::error;
    }

public:
    TlsConnection(TlsContext& context, const TcpConnection& connection,
                  TlsRole role,
                  const std::string& expected_peer_name = {})
        : ssl_(SSL_new(context.native_context())) {
        if (!ssl_ || SSL_set_fd(ssl_.get(),
                static_cast<int>(connection.native_socket())) != 1) {
            ssl_.reset();
            return;
        }
        if (!expected_peer_name.empty() &&
            SSL_set1_host(ssl_.get(), expected_peer_name.c_str()) != 1) {
            ssl_.reset();
            return;
        }
        if (role == TlsRole::server) SSL_set_accept_state(ssl_.get());
        else SSL_set_connect_state(ssl_.get());
    }

    TlsStatus handshake() {
        if (!ssl_) return TlsStatus::error;
        const auto result = SSL_do_handshake(ssl_.get());
        if (result != 1) return classify(result);
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
        X509* peer = SSL_get1_peer_certificate(ssl_.get());
#else
        X509* peer = SSL_get_peer_certificate(ssl_.get());
#endif
        if (peer == nullptr || SSL_get_verify_result(ssl_.get()) != X509_V_OK) {
            X509_free(peer);
            return TlsStatus::error;
        }
        X509_free(peer);
        authenticated_ = true;
        return TlsStatus::ok;
    }

    TlsIoResult send_some(const std::uint8_t* data, std::size_t size) {
        if (!authenticated_) return {};
        std::size_t written = 0U;
        const auto result = SSL_write_ex(ssl_.get(), data, size, &written);
        return result == 1 ? TlsIoResult{TlsStatus::ok, written}
                           : TlsIoResult{classify(result), 0U};
    }
    TlsIoResult receive_some(std::uint8_t* data, std::size_t size) {
        if (!authenticated_) return {};
        std::size_t received = 0U;
        const auto result = SSL_read_ex(ssl_.get(), data, size, &received);
        return result == 1 ? TlsIoResult{TlsStatus::ok, received}
                           : TlsIoResult{classify(result), 0U};
    }
    bool authenticated() const noexcept { return authenticated_; }
    const char* cipher() const noexcept {
        return authenticated_ ? SSL_get_cipher_name(ssl_.get()) : nullptr;
    }
};

class TlsPeerTransport final : public PeerTransport {
    TcpConnection connection_;
    TlsConnection tls_;
public:
    TlsPeerTransport(TlsContext& context, TcpConnection connection, TlsRole role,
                     const std::string& expected_peer_name = {})
        : connection_(std::move(connection)),
          tls_(context, connection_, role, expected_peer_name) {}

    TlsStatus handshake() { return tls_.handshake(); }
    const char* cipher() const noexcept { return tls_.cipher(); }
    SocketIoResult send_some(const std::uint8_t* data,
                             std::size_t size) override {
        const auto result = tls_.send_some(data, size);
        switch (result.status) {
            case TlsStatus::ok: return {SocketIoStatus::ok, result.bytes};
            case TlsStatus::would_block: return {SocketIoStatus::would_block, 0U};
            case TlsStatus::closed: return {SocketIoStatus::closed, 0U};
            case TlsStatus::error: return {SocketIoStatus::error, 0U};
        }
        return {SocketIoStatus::error, 0U};
    }
    SocketIoResult receive_some(std::uint8_t* data,
                                std::size_t size) override {
        const auto result = tls_.receive_some(data, size);
        switch (result.status) {
            case TlsStatus::ok: return {SocketIoStatus::ok, result.bytes};
            case TlsStatus::would_block: return {SocketIoStatus::would_block, 0U};
            case TlsStatus::closed: return {SocketIoStatus::closed, 0U};
            case TlsStatus::error: return {SocketIoStatus::error, 0U};
        }
        return {SocketIoStatus::error, 0U};
    }
    bool authenticated() const noexcept override { return tls_.authenticated(); }
};

} // namespace onuros
