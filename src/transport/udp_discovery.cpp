#include "openomada/transport/udp_discovery.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <vector>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include "openomada/protocol/frame_codec.hpp"

namespace openomada::transport {
namespace {

TransportStatus errno_failure(const char* prefix) {
    std::string error(prefix);
    error += ": ";
    error += std::strerror(errno);
    return TransportStatus::failure(error);
}

} // namespace

TransportStatus UdpDiscoveryTransport::open(const UdpDiscoveryOptions& options) {
    close();
    timeout_seconds_ = options.timeout_seconds;

    SocketFd candidate(socket(AF_INET, SOCK_DGRAM, 0));
    if (!candidate.valid()) {
        return errno_failure("socket");
    }
    std::string error;
    if (!set_socket_timeout(candidate.get(), timeout_seconds_, &error)) {
        return TransportStatus::failure(error);
    }
    if (options.local_port != 0) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(options.local_port);
        if (bind(candidate.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            return errno_failure("bind");
        }
    }

    socket_ = std::move(candidate);
    return TransportStatus::success();
}

TransportStatus UdpDiscoveryTransport::send_payload_to(const UdpEndpoint& endpoint, std::string_view payload) {
    if (!socket_.valid()) {
        return TransportStatus::failure("UDP discovery socket is not open");
    }
    if (payload.size() > protocol::kMaxDiscoveryPayload) {
        return TransportStatus::failure("discovery payload exceeds ECSP UDP safety limit");
    }

    addrinfo hints{};
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_UNSPEC;

    addrinfo* addresses = nullptr;
    const std::string port_text = std::to_string(endpoint.port);
    const int gai = getaddrinfo(endpoint.host.c_str(), port_text.c_str(), &hints, &addresses);
    if (gai != 0) {
        std::string error("getaddrinfo: ");
        error += gai_strerror(gai);
        return TransportStatus::failure(error);
    }

    const std::vector<std::uint8_t> frame = protocol::encode_frame(payload);
    bool sent = false;
    std::string last_error;
    for (addrinfo* ai = addresses; ai != nullptr; ai = ai->ai_next) {
        const ssize_t rc = sendto(socket_.get(), frame.data(), frame.size(), 0, ai->ai_addr, ai->ai_addrlen);
        if (rc == static_cast<ssize_t>(frame.size())) {
            sent = true;
            break;
        }
        last_error = std::strerror(errno);
    }
    freeaddrinfo(addresses);
    if (!sent) {
        return TransportStatus::failure(last_error.empty() ? "sendto failed" : last_error);
    }
    return TransportStatus::success();
}

ReceiveResult UdpDiscoveryTransport::receive_payload() {
    ReceiveResult result;
    if (!socket_.valid()) {
        result.error = "UDP discovery socket is not open";
        return result;
    }
    std::array<std::uint8_t, 65535> buffer{};
    const ssize_t rc = recv(socket_.get(), buffer.data(), buffer.size(), 0);
    if (rc < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            result.timeout = true;
            result.error = "UDP receive timeout";
        } else {
            result.error = std::strerror(errno);
        }
        return result;
    }
    auto decoded = protocol::decode_frame(buffer.data(), static_cast<std::size_t>(rc), protocol::kMaxDiscoveryPayload);
    if (!decoded.ok()) {
        result.error = "ECSP UDP frame decode failed";
        return result;
    }
    result.ok = true;
    result.payload = std::move(decoded.payload);
    return result;
}

} // namespace openomada::transport

