#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openomada/application/configuration.hpp"

namespace openomada::platform {

struct ToolAvailability {
    bool uci{false};
    bool ubus{false};
    bool nft{false};
    bool hostapd{false};
    bool dnsmasq{false};
    bool opennds{false};
    bool ndsctl{false};
    bool iw{false};
};

struct PlatformCapabilityInput {
    std::string platform{"auto"};
    std::string radio_bands{"2g"};
    std::uint32_t max_ssids{4};
    ToolAvailability tools{};
    std::string iw_list_output{};

    std::optional<bool> cap_wlan{};
    std::optional<bool> cap_wpa2_psk{};
    std::optional<bool> cap_wpa3_psk{};
    std::optional<bool> cap_wpa_enterprise{};
    std::optional<bool> cap_radius{};
    std::optional<bool> cap_ssid_vlan{};
    std::optional<bool> cap_dynamic_vlan{};
    std::optional<bool> cap_management_vlan{};
    std::optional<bool> cap_portal{};
    std::optional<bool> cap_dhcp_tracking{};
    std::optional<bool> cap_option82{};
    std::optional<bool> cap_led{};
    std::optional<bool> cap_client_operations{};
    std::optional<bool> cap_client_rate_limits{};
};

struct PlatformCapabilities {
    std::string platform{"generic"};
    ToolAvailability tools{};
    std::vector<application::RadioBand> radio_bands{};
    std::uint32_t max_ssids{0};
    bool supports_wlan_config{false};
    bool supports_wpa2_psk{false};
    bool supports_wpa3_psk{false};
    bool supports_wpa_enterprise{false};
    bool supports_radius{false};
    bool supports_ssid_vlan{false};
    bool supports_dynamic_vlan{false};
    bool supports_management_vlan{false};
    bool supports_portal{false};
    bool supports_dhcp_tracking{false};
    bool supports_option82{false};
    bool supports_led_control{false};
    bool supports_client_operations{false};
    bool supports_client_rate_limits{false};
};

struct CapabilityResult {
    bool ok{false};
    PlatformCapabilities capabilities{};
    std::string error{};
};

std::optional<application::RadioBand> radio_band_from_wire(std::string_view value) noexcept;
std::vector<application::RadioBand> parse_radio_bands(std::string_view raw, std::string* error = nullptr);
std::optional<std::uint32_t> iw_ap_interface_limit(std::string_view iw_list_output) noexcept;
CapabilityResult detect_platform_capabilities(const PlatformCapabilityInput& input) noexcept;
std::string capability_summary(const PlatformCapabilities& capabilities);
bool supports_radio_band(const PlatformCapabilities& capabilities, application::RadioBand band) noexcept;

} // namespace openomada::platform
