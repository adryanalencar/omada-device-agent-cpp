#include "openomada/platform/capabilities.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace openomada::platform {
namespace {

std::string lower_trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    std::string out;
    out.reserve(end - begin);
    for (std::size_t index = begin; index < end; ++index) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value[index]))));
    }
    return out;
}

bool has_line(std::string_view text, std::string_view needle) noexcept {
    std::size_t offset = 0;
    while (offset <= text.size()) {
        const std::size_t next = text.find('\n', offset);
        const std::size_t end = next == std::string_view::npos ? text.size() : next;
        const std::string line = lower_trim(text.substr(offset, end - offset));
        if (line == needle) {
            return true;
        }
        if (next == std::string_view::npos) {
            break;
        }
        offset = next + 1;
    }
    return false;
}

bool contains(std::string_view text, std::string_view needle) noexcept {
    return text.find(needle) != std::string_view::npos;
}

std::optional<std::uint32_t> parse_uint(std::string_view text) noexcept {
    std::uint32_t value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc() || result.ptr != last) {
        return std::nullopt;
    }
    return value;
}

bool group_has_ap(std::string_view group) {
    std::size_t offset = 0;
    while (offset <= group.size()) {
        const std::size_t comma = group.find(',', offset);
        const std::size_t end = comma == std::string_view::npos ? group.size() : comma;
        if (lower_trim(group.substr(offset, end - offset)) == "ap") {
            return true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        offset = comma + 1;
    }
    return false;
}

bool contains_band(const std::vector<application::RadioBand>& bands, application::RadioBand band) noexcept {
    return std::find(bands.begin(), bands.end(), band) != bands.end();
}

std::string feature_name(std::string_view raw) {
    constexpr std::string_view prefix = "supports_";
    if (raw.substr(0, prefix.size()) == prefix) {
        raw.remove_prefix(prefix.size());
    }
    return std::string(raw);
}

void append_feature(std::string& out, bool& first, std::string_view name, bool enabled) {
    if (!enabled) {
        return;
    }
    if (!first) {
        out.push_back(',');
    }
    out += feature_name(name);
    first = false;
}

bool resolved(std::optional<bool> override_value, bool default_value) noexcept {
    return override_value.value_or(default_value);
}

} // namespace

std::optional<application::RadioBand> radio_band_from_wire(std::string_view value) noexcept {
    const std::string normalized = lower_trim(value);
    if (normalized == "2g" || normalized == "2g4") {
        return application::RadioBand::TwoG;
    }
    if (normalized == "5g") {
        return application::RadioBand::FiveG;
    }
    if (normalized == "5g2") {
        return application::RadioBand::FiveG2;
    }
    if (normalized == "6g") {
        return application::RadioBand::SixG;
    }
    return std::nullopt;
}

std::vector<application::RadioBand> parse_radio_bands(std::string_view raw, std::string* error) {
    std::vector<application::RadioBand> bands;
    std::size_t offset = 0;
    while (offset <= raw.size()) {
        const std::size_t comma = raw.find(',', offset);
        const std::size_t end = comma == std::string_view::npos ? raw.size() : comma;
        const std::string value = lower_trim(raw.substr(offset, end - offset));
        if (!value.empty()) {
            auto band = radio_band_from_wire(value);
            if (!band.has_value()) {
                if (error != nullptr) {
                    *error = "unsupported radio band: " + value;
                }
                return {};
            }
            if (!contains_band(bands, *band)) {
                bands.push_back(*band);
            }
        }
        if (comma == std::string_view::npos) {
            break;
        }
        offset = comma + 1;
    }
    return bands;
}

std::optional<std::uint32_t> iw_ap_interface_limit(std::string_view iw_list_output) noexcept {
    if (contains(iw_list_output, "interface combinations are not supported")) {
        return has_line(iw_list_output, "* ap") ? std::optional<std::uint32_t>(1U) : std::nullopt;
    }

    std::vector<std::uint32_t> limits;
    std::size_t offset = 0;
    while ((offset = iw_list_output.find("#{", offset)) != std::string_view::npos) {
        const std::size_t group_end = iw_list_output.find('}', offset + 2);
        if (group_end == std::string_view::npos) {
            break;
        }
        const std::size_t less_equal = iw_list_output.find("<=", group_end + 1);
        if (less_equal == std::string_view::npos) {
            offset = group_end + 1;
            continue;
        }
        const std::string_view group = iw_list_output.substr(offset + 2, group_end - (offset + 2));
        std::size_t number_begin = less_equal + 2;
        while (number_begin < iw_list_output.size() &&
               std::isspace(static_cast<unsigned char>(iw_list_output[number_begin])) != 0) {
            ++number_begin;
        }
        std::size_t number_end = number_begin;
        while (number_end < iw_list_output.size() &&
               std::isdigit(static_cast<unsigned char>(iw_list_output[number_end])) != 0) {
            ++number_end;
        }
        if (group_has_ap(group)) {
            if (auto parsed = parse_uint(iw_list_output.substr(number_begin, number_end - number_begin))) {
                limits.push_back(*parsed);
            }
        }
        offset = group_end + 1;
    }
    if (limits.empty()) {
        return std::nullopt;
    }
    return *std::max_element(limits.begin(), limits.end());
}

CapabilityResult detect_platform_capabilities(const PlatformCapabilityInput& input) noexcept {
    CapabilityResult result;
    const std::string platform = lower_trim(input.platform);
    if (platform != "auto" && platform != "openwrt" && platform != "generic") {
        result.error = "platform must be auto, openwrt or generic";
        return result;
    }

    std::string band_error;
    auto bands = parse_radio_bands(input.radio_bands, &band_error);
    if (!band_error.empty()) {
        result.error = band_error;
        return result;
    }

    const bool openwrt = platform == "openwrt" ||
        (platform == "auto" && input.tools.uci && input.tools.ubus);
    std::uint32_t max_ssids = input.max_ssids;
    if (openwrt && input.tools.iw && max_ssids > 1) {
        if (auto limit = iw_ap_interface_limit(input.iw_list_output)) {
            max_ssids = std::min(max_ssids, *limit);
        }
    }
    const bool wlan_possible = openwrt && input.tools.uci;
    const bool has_opennds = input.tools.opennds && input.tools.ndsctl;

    PlatformCapabilities capabilities;
    capabilities.platform = openwrt ? "openwrt" : platform;
    capabilities.tools = input.tools;
    capabilities.tools.opennds = has_opennds;
    capabilities.radio_bands = std::move(bands);
    capabilities.max_ssids = max_ssids;
    capabilities.supports_wlan_config = resolved(input.cap_wlan, wlan_possible);
    capabilities.supports_wpa2_psk = resolved(input.cap_wpa2_psk, wlan_possible);
    capabilities.supports_wpa3_psk = resolved(input.cap_wpa3_psk, false);
    capabilities.supports_wpa_enterprise = resolved(input.cap_wpa_enterprise, false);
    capabilities.supports_radius = resolved(input.cap_radius, false);
    capabilities.supports_ssid_vlan = resolved(input.cap_ssid_vlan, false);
    capabilities.supports_dynamic_vlan = resolved(input.cap_dynamic_vlan, false);
    capabilities.supports_management_vlan = resolved(input.cap_management_vlan, false);
    capabilities.supports_portal = resolved(input.cap_portal, openwrt && has_opennds);
    capabilities.supports_dhcp_tracking = resolved(input.cap_dhcp_tracking, openwrt && input.tools.ubus);
    capabilities.supports_option82 = resolved(input.cap_option82, false);
    capabilities.supports_led_control = resolved(input.cap_led, false);
    capabilities.supports_client_operations = resolved(input.cap_client_operations, openwrt && input.tools.ubus);
    capabilities.supports_client_rate_limits = resolved(input.cap_client_rate_limits, false);

    result.ok = true;
    result.capabilities = std::move(capabilities);
    return result;
}

std::string capability_summary(const PlatformCapabilities& capabilities) {
    std::string bands;
    for (std::size_t index = 0; index < capabilities.radio_bands.size(); ++index) {
        if (index != 0) {
            bands.push_back(',');
        }
        bands += application::to_wire_string(capabilities.radio_bands[index]);
    }
    if (bands.empty()) {
        bands = "none";
    }

    std::string features;
    bool first = true;
    append_feature(features, first, "supports_wlan_config", capabilities.supports_wlan_config);
    append_feature(features, first, "supports_wpa2_psk", capabilities.supports_wpa2_psk);
    append_feature(features, first, "supports_wpa3_psk", capabilities.supports_wpa3_psk);
    append_feature(features, first, "supports_wpa_enterprise", capabilities.supports_wpa_enterprise);
    append_feature(features, first, "supports_radius", capabilities.supports_radius);
    append_feature(features, first, "supports_ssid_vlan", capabilities.supports_ssid_vlan);
    append_feature(features, first, "supports_dynamic_vlan", capabilities.supports_dynamic_vlan);
    append_feature(features, first, "supports_management_vlan", capabilities.supports_management_vlan);
    append_feature(features, first, "supports_portal", capabilities.supports_portal);
    append_feature(features, first, "supports_dhcp_tracking", capabilities.supports_dhcp_tracking);
    append_feature(features, first, "supports_option82", capabilities.supports_option82);
    append_feature(features, first, "supports_led_control", capabilities.supports_led_control);
    append_feature(features, first, "supports_client_operations", capabilities.supports_client_operations);
    append_feature(features, first, "supports_client_rate_limits", capabilities.supports_client_rate_limits);
    if (features.empty()) {
        features = "none";
    }

    std::string out = "platform=" + capabilities.platform;
    out += " bands=" + bands;
    out += " maxSsids=" + std::to_string(capabilities.max_ssids);
    out += " tools=uci:" + std::to_string(capabilities.tools.uci ? 1 : 0);
    out += ",ubus:" + std::to_string(capabilities.tools.ubus ? 1 : 0);
    out += ",nft:" + std::to_string(capabilities.tools.nft ? 1 : 0);
    out += ",hostapd:" + std::to_string(capabilities.tools.hostapd ? 1 : 0);
    out += ",dnsmasq:" + std::to_string(capabilities.tools.dnsmasq ? 1 : 0);
    out += ",opennds:" + std::to_string(capabilities.tools.opennds ? 1 : 0);
    out += " features=" + features;
    return out;
}

bool supports_radio_band(const PlatformCapabilities& capabilities, application::RadioBand band) noexcept {
    return contains_band(capabilities.radio_bands, band);
}

} // namespace openomada::platform
