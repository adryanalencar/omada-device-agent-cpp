#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "openomada/application/settings.hpp"
#include "openomada/domain/device_profile.hpp"
#include "openomada/transport/udp_discovery.hpp"

namespace openomada::lifecycle {

struct PreAdoptRequest {
    bool ok{false};
    bool is_pre_adopt{false};
    std::uint16_t adopt_port{29814};
    std::string controller_id{};
    std::string destination_id{};
    std::uint32_t seq{0};
    std::string error{};
};

PreAdoptRequest parse_pre_adopt_request(
    std::string_view payload,
    const std::string& current_controller_id,
    std::uint16_t default_manage_port
);

transport::TransportStatus send_discovery_once(
    transport::UdpDiscoveryTransport& transport,
    const transport::UdpEndpoint& endpoint,
    const application::AgentSettings& settings,
    const domain::AccessPointProfile& profile,
    std::uint32_t seq,
    bool managed_restart,
    std::uint64_t timestamp_ms = 0
);

} // namespace openomada::lifecycle

