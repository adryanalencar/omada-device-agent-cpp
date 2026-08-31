#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "openomada/transport/frame_transport.hpp"
#include "openomada/transport/socket.hpp"

namespace openomada::transport {

struct UdpEndpoint {
    std::string host;
    std::uint16_t port{29810};
};

struct UdpDiscoveryOptions {
    std::uint16_t local_port{0};
    std::uint32_t timeout_seconds{1};
};

class UdpDiscoveryTransport {
public:
    TransportStatus open(const UdpDiscoveryOptions& options);
    TransportStatus send_payload_to(const UdpEndpoint& endpoint, std::string_view payload);
    ReceiveResult receive_payload();
    void close() noexcept { socket_.reset(); }

private:
    SocketFd socket_{};
    std::uint32_t timeout_seconds_{1};
};

} // namespace openomada::transport

