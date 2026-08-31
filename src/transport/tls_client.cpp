#include "openomada/transport/tls_client.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <vector>

#include <openssl/err.h>

#include "openomada/protocol/frame_codec.hpp"

namespace openomada::transport {
namespace {

std::string openssl_error(const char* prefix) {
    std::string error(prefix);
    const unsigned long code = ERR_get_error();
    if (code == 0) {
        error += ": ";
        error += std::strerror(errno);
        return error;
    }
    std::array<char, 256> buffer{};
    ERR_error_string_n(code, buffer.data(), buffer.size());
    error += ": ";
    error += buffer.data();
    return error;
}

bool ssl_read_exact(SSL* ssl, std::uint8_t* buffer, std::size_t size, bool* timeout, std::string* error) {
    if (timeout != nullptr) {
        *timeout = false;
    }
    std::size_t offset = 0;
    while (offset < size) {
        const int rc = SSL_read(ssl, buffer + offset, static_cast<int>(size - offset));
        if (rc > 0) {
            offset += static_cast<std::size_t>(rc);
            continue;
        }
        const int ssl_error = SSL_get_error(ssl, rc);
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            if (timeout != nullptr) {
                *timeout = true;
            }
            if (error != nullptr) {
                *error = "TLS read timeout";
            }
            return false;
        }
        if (ssl_error == SSL_ERROR_ZERO_RETURN) {
            if (error != nullptr) {
                *error = "TLS peer closed connection";
            }
            return false;
        }
        if (error != nullptr) {
            *error = openssl_error("SSL_read");
        }
        return false;
    }
    return true;
}

bool ssl_write_all(SSL* ssl, const std::uint8_t* buffer, std::size_t size, std::string* error) {
    std::size_t offset = 0;
    while (offset < size) {
        const int rc = SSL_write(ssl, buffer + offset, static_cast<int>(size - offset));
        if (rc > 0) {
            offset += static_cast<std::size_t>(rc);
            continue;
        }
        const int ssl_error = SSL_get_error(ssl, rc);
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        if (error != nullptr) {
            *error = openssl_error("SSL_write");
        }
        return false;
    }
    return true;
}

} // namespace

TlsFrameTransport::~TlsFrameTransport() {
    close();
}

TransportStatus TlsFrameTransport::connect(const TlsClientOptions& options) {
    close();
    std::string error;
    if (!connect_tcp(socket_, options.host, options.port, options.timeout_seconds, &error)) {
        return TransportStatus::failure(error);
    }

    OPENSSL_init_ssl(0, nullptr);

    ctx_ = SSL_CTX_new(TLS_client_method());
    if (ctx_ == nullptr) {
        close();
        return TransportStatus::failure(openssl_error("SSL_CTX_new"));
    }
    SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx_, TLS1_2_VERSION);

    if (options.verify_peer) {
        SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER, nullptr);
        const int loaded = options.ca_file.empty()
            ? SSL_CTX_set_default_verify_paths(ctx_)
            : SSL_CTX_load_verify_locations(ctx_, options.ca_file.c_str(), nullptr);
        if (loaded != 1) {
            close();
            return TransportStatus::failure(openssl_error("SSL_CTX_load_verify_locations"));
        }
    } else {
        SSL_CTX_set_verify(ctx_, SSL_VERIFY_NONE, nullptr);
    }

    ssl_ = SSL_new(ctx_);
    if (ssl_ == nullptr) {
        close();
        return TransportStatus::failure(openssl_error("SSL_new"));
    }
    SSL_set_fd(ssl_, socket_.get());
    if (!options.host.empty()) {
        SSL_set_tlsext_host_name(ssl_, options.host.c_str());
    }

    if (SSL_connect(ssl_) != 1) {
        const std::string message = openssl_error("SSL_connect");
        close();
        return TransportStatus::failure(message);
    }
    return TransportStatus::success();
}

TransportStatus TlsFrameTransport::send_payload(std::string_view payload) {
    if (ssl_ == nullptr) {
        return TransportStatus::failure("TLS transport is not connected");
    }
    const std::vector<std::uint8_t> frame = protocol::encode_frame(payload);
    std::string error;
    if (!ssl_write_all(ssl_, frame.data(), frame.size(), &error)) {
        return TransportStatus::failure(error);
    }
    return TransportStatus::success();
}

ReceiveResult TlsFrameTransport::receive_payload() {
    ReceiveResult result;
    if (ssl_ == nullptr) {
        result.error = "TLS transport is not connected";
        return result;
    }

    std::array<std::uint8_t, protocol::kLengthSize> prefix{};
    bool timeout = false;
    std::string error;
    if (!ssl_read_exact(ssl_, prefix.data(), prefix.size(), &timeout, &error)) {
        result.timeout = timeout;
        result.error = error;
        return result;
    }
    const std::uint32_t length =
        (static_cast<std::uint32_t>(prefix[0]) << 24) |
        (static_cast<std::uint32_t>(prefix[1]) << 16) |
        (static_cast<std::uint32_t>(prefix[2]) << 8) |
        static_cast<std::uint32_t>(prefix[3]);
    if (length == 0 || length > protocol::kMaxTcpPayload) {
        result.error = "invalid ECSP TCP payload length";
        return result;
    }

    std::vector<std::uint8_t> frame;
    frame.reserve(protocol::kLengthSize + length);
    frame.insert(frame.end(), prefix.begin(), prefix.end());
    const std::size_t body_offset = frame.size();
    frame.resize(protocol::kLengthSize + length);
    if (!ssl_read_exact(ssl_, frame.data() + body_offset, length, &timeout, &error)) {
        result.timeout = timeout;
        result.error = error;
        return result;
    }
    auto decoded = protocol::decode_frame(frame);
    if (!decoded.ok()) {
        result.error = "ECSP frame decode failed";
        return result;
    }
    result.ok = true;
    result.payload = std::move(decoded.payload);
    return result;
}

void TlsFrameTransport::close() noexcept {
    if (ssl_ != nullptr) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (ctx_ != nullptr) {
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
    }
    socket_.reset();
}

} // namespace openomada::transport

