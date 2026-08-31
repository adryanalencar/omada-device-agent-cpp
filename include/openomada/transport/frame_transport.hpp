#pragma once

#include <string>
#include <string_view>

namespace openomada::transport {

struct TransportStatus {
    bool ok{false};
    std::string error{};

    static TransportStatus success() { return {true, {}}; }
    static TransportStatus failure(std::string message) { return {false, std::move(message)}; }
};

struct ReceiveResult {
    bool ok{false};
    bool timeout{false};
    std::string payload{};
    std::string error{};
};

class FrameTransport {
public:
    virtual ~FrameTransport() = default;

    virtual TransportStatus send_payload(std::string_view payload) = 0;
    virtual ReceiveResult receive_payload() = 0;
};

} // namespace openomada::transport

