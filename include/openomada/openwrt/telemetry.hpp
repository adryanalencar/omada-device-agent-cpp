#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openomada/application/client_state.hpp"
#include "openomada/application/inform.hpp"

namespace openomada::openwrt {

struct OpenWrtWirelessInterface {
    std::string ifname{};
    std::string ssid{};
    std::optional<application::RadioBand> band{};
};

struct WirelessInterfaceParseResult {
    bool ok{false};
    std::vector<OpenWrtWirelessInterface> interfaces{};
    std::string error{};
};

struct WirelessInformResult {
    bool ok{false};
    std::vector<application::JsonBodyMember> members{};
    std::string error{};
};

struct HostapdClientResult {
    bool ok{false};
    std::vector<application::WirelessClientState> clients{};
    std::string error{};
};

WirelessInformResult openwrt_wireless_inform_from_status_json(std::string_view status_json) noexcept;
WirelessInterfaceParseResult openwrt_wireless_interfaces_from_status_json(std::string_view status_json) noexcept;
HostapdClientResult hostapd_client_states_from_json(
    const OpenWrtWirelessInterface& interface,
    std::string_view hostapd_json
) noexcept;
WirelessInformResult openwrt_wireless_inform_from_status_and_hostapd_json(
    std::string_view status_json,
    const std::vector<std::string>& hostapd_client_json_by_interface
) noexcept;

} // namespace openomada::openwrt
