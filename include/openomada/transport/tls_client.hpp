#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <openssl/ssl.h>

#include "openomada/transport/frame_transport.hpp"
#include "openomada/transport/socket.hpp"

namespace openomada::transport {

struct TlsClientOptions {
    std::string host;
    std::uint16_t port{29814};
    std::uint32_t timeout_seconds{15};
    bool verify_peer{false};
    std::string ca_file{};
};

class TlsFrameTransport final : public FrameTransport {
public:
    TlsFrameTransport() = default;
    ~TlsFrameTransport() override;

    TlsFrameTransport(const TlsFrameTransport&) = delete;
    TlsFrameTransport& operator=(const TlsFrameTransport&) = delete;

    TransportStatus connect(const TlsClientOptions& options);
    TransportStatus send_payload(std::string_view payload) override;
    ReceiveResult receive_payload() override;
    void close() noexcept;

private:
    SocketFd socket_{};
    SSL_CTX* ctx_{nullptr};
    SSL* ssl_{nullptr};
};

} // namespace openomada::transport

