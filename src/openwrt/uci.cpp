#include "openomada/openwrt/uci.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace openomada::openwrt {
namespace {

bool has_band(
    const platform::PlatformCapabilities& capabilities,
    application::RadioBand band
) noexcept {
    return platform::supports_radio_band(capabilities, band);
}

std::string radio_section(application::RadioBand band, std::optional<std::int64_t> radio_id) {
    if (radio_id.has_value()) {
        return "radio" + std::to_string(*radio_id);
    }
    switch (band) {
    case application::RadioBand::TwoG:
        return "radio0";
    case application::RadioBand::FiveG:
        return "radio1";
    case application::RadioBand::FiveG2:
        return "radio2";
    case application::RadioBand::SixG:
        return "radio3";
    }
    return "radio0";
}

std::string slug(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    bool last_underscore = false;
    for (char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) != 0 || ch == '_') {
            out.push_back(static_cast<char>(std::tolower(uch)));
            last_underscore = false;
        } else if (!last_underscore) {
            out.push_back('_');
            last_underscore = true;
        }
    }
    while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out;
}

std::string wlan_section(const application::WirelessNetwork& wlan) {
    std::string suffix;
    if (wlan.index.has_value()) {
        suffix = std::to_string(*wlan.index);
    } else if (wlan.ssid_id.has_value()) {
        suffix = std::to_string(*wlan.ssid_id);
    } else {
        suffix = slug(wlan.name);
        if (suffix.empty()) {
            suffix = "ssid";
        }
    }
    std::string section = slug("openomada_" + std::string(application::to_wire_string(wlan.band)) + "_" + suffix);
    if (section.size() > 48) {
        section.resize(48);
        while (!section.empty() && section.back() == '_') {
            section.pop_back();
        }
    }
    return section.empty() ? "openomada_ssid" : section;
}

std::string quote_uci(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        if (ch == '\\') {
            out += "\\\\";
        } else if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

std::string set_line(std::string_view config, std::string_view section, std::string_view option, std::string_view value) {
    std::string out = "set ";
    out += config;
    out.push_back('.');
    out += section;
    out.push_back('.');
    out += option;
    out += "='";
    out += quote_uci(value);
    out.push_back('\'');
    return out;
}

std::string htmode(std::int64_t width) {
    if (width <= 20) {
        return "HT20";
    }
    if (width <= 40) {
        return "HT40";
    }
    if (width <= 80) {
        return "VHT80";
    }
    return "HE" + std::to_string(width);
}

std::size_t max_active_ssids(
    const application::AccessPointConfigUpdate& update,
    const platform::PlatformCapabilities& capabilities
) noexcept {
    const std::size_t requested = update.wlans.size();
    const std::size_t capacity = static_cast<std::size_t>(capabilities.max_ssids);
    return std::min(requested, capacity);
}

std::vector<application::WirelessNetwork> active_wlans(
    const application::AccessPointConfigUpdate& update,
    const platform::PlatformCapabilities& capabilities
) {
    const std::size_t active_count = max_active_ssids(update, capabilities);
    return {update.wlans.begin(), update.wlans.begin() + static_cast<std::ptrdiff_t>(active_count)};
}

void append_unique(std::vector<std::string>& values, std::string value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}

void add_error(std::vector<std::string>& errors, std::string error) {
    if (std::find(errors.begin(), errors.end(), error) == errors.end()) {
        errors.push_back(std::move(error));
    }
}

bool has_radius_request(const application::WirelessSecurity& security) noexcept {
    return security.radius_auth || security.radius_accounting || security.radius_mac_auth;
}

std::vector<std::string> radio_lines(const application::RadioConfig& radio) {
    std::vector<std::string> lines;
    const std::string section = radio_section(radio.band, radio.radio_id);
    if (radio.enabled.has_value()) {
        lines.push_back(set_line("wireless", section, "disabled", *radio.enabled ? "0" : "1"));
    }
    if (radio.channel.has_value()) {
        lines.push_back(set_line("wireless", section, "channel", std::to_string(*radio.channel)));
    }
    if (radio.channel_width.has_value()) {
        lines.push_back(set_line("wireless", section, "htmode", htmode(*radio.channel_width)));
    }
    if (radio.tx_power.has_value()) {
        lines.push_back(set_line("wireless", section, "txpower", std::to_string(*radio.tx_power)));
    }
    return lines;
}

std::vector<std::string> ssid_vlan_lines(std::uint16_t vlan_id) {
    const std::string section = "openomada_vlan" + std::to_string(vlan_id);
    return {
        "delete network." + section,
        "set network." + section + "=interface",
        set_line("network", section, "proto", "none"),
        set_line("network", section, "device", "br-lan." + std::to_string(vlan_id)),
    };
}

std::vector<std::string> management_vlan_lines(
    const application::ManagementVlan& vlan,
    const UciPlanOptions& options
) {
    if (options.management_vlan_interface.empty() || options.management_vlan_device.empty()) {
        return {};
    }
    if (!vlan.enabled) {
        return {
            set_line("network", options.management_vlan_interface, "device", options.management_vlan_device),
        };
    }
    if (!vlan.vlan_id.has_value()) {
        return {};
    }
    return {
        set_line(
            "network",
            options.management_vlan_interface,
            "device",
            options.management_vlan_device + "." + std::to_string(*vlan.vlan_id)
        ),
    };
}

std::vector<std::string> wlan_lines(
    const application::WirelessNetwork& wlan,
    const platform::PlatformCapabilities& capabilities
) {
    const std::string section = wlan_section(wlan);
    const std::string device = radio_section(wlan.band, wlan.radio_id);
    std::string network = "lan";
    if (wlan.vlan.vlan_id.has_value() && capabilities.supports_ssid_vlan) {
        network = "openomada_vlan" + std::to_string(*wlan.vlan.vlan_id);
    }

    std::vector<std::string> lines;
    lines.push_back("delete wireless." + section);
    lines.push_back("set wireless." + section + "=wifi-iface");
    lines.push_back(set_line("wireless", section, "openomada_managed", "1"));
    lines.push_back(set_line("wireless", section, "device", device));
    lines.push_back(set_line("wireless", section, "mode", "ap"));
    lines.push_back(set_line("wireless", section, "network", network));
    lines.push_back(set_line("wireless", section, "ssid", wlan.name));
    lines.push_back(set_line("wireless", section, "hidden", wlan.broadcast.has_value() && !*wlan.broadcast ? "1" : "0"));
    if (wlan.client_isolation.has_value()) {
        lines.push_back(set_line("wireless", section, "isolate", *wlan.client_isolation ? "1" : "0"));
    }

    if (wlan.security.psk_configured) {
        lines.push_back(set_line(
            "wireless",
            section,
            "encryption",
            wlan.security.psk_version.value_or(2) == 3 ? "sae-mixed" : "psk2"
        ));
        if (!wlan.security.psk_key.empty()) {
            lines.push_back(set_line("wireless", section, "key", wlan.security.psk_key));
        }
    } else {
        lines.push_back(set_line("wireless", section, "encryption", "none"));
    }
    return lines;
}

} // namespace

UciValidationResult validate_update(
    const application::AccessPointConfigUpdate& update,
    const platform::PlatformCapabilities& capabilities,
    const UciPlanOptions& options
) {
    UciValidationResult result;

    if ((!update.radios.empty() || !update.wlans.empty()) && !capabilities.supports_wlan_config) {
        add_error(result.errors, "platform does not support WLAN configuration");
    }

    for (const auto& radio : update.radios) {
        if (!has_band(capabilities, radio.band)) {
            add_error(result.errors, std::string("radio band ") + application::to_wire_string(radio.band) + " is not supported");
        }
    }

    for (const auto& wlan : active_wlans(update, capabilities)) {
        if (!has_band(capabilities, wlan.band)) {
            add_error(result.errors, std::string("SSID band ") + application::to_wire_string(wlan.band) + " is not supported");
        }
        if (wlan.vlan.vlan_id.has_value() && !capabilities.supports_ssid_vlan) {
            add_error(result.errors, "SSID VLAN requested but platform capability is disabled");
        }
        if (wlan.vlan.dynamic_vlan_mode.has_value() && *wlan.vlan.dynamic_vlan_mode != 0 &&
            !capabilities.supports_dynamic_vlan) {
            add_error(result.errors, "dynamic VLAN requested but platform capability is disabled");
        }
        if (wlan.vlan.dhcp_option82.has_value() && wlan.vlan.dhcp_option82->enabled &&
            !capabilities.supports_option82) {
            add_error(result.errors, "DHCP Option 82 requested but platform capability is disabled");
        }
        if (wlan.portal.enabled && !capabilities.supports_portal) {
            add_error(result.errors, "portal WLAN requested but platform capability is disabled");
        }
        if (wlan.security.psk_configured && wlan.security.psk_version.value_or(2) == 3 &&
            !capabilities.supports_wpa3_psk) {
            add_error(result.errors, "WPA3-PSK WLAN requested but WPA3-PSK capability is disabled");
        }
        if (wlan.security.psk_configured && wlan.security.psk_version.value_or(2) != 3 &&
            !capabilities.supports_wpa2_psk) {
            add_error(result.errors, "PSK WLAN requested but WPA2-PSK capability is disabled");
        }
        if (has_radius_request(wlan.security) && !capabilities.supports_radius) {
            add_error(result.errors, "RADIUS WLAN requested but RADIUS capability is disabled");
        }
    }

    if (update.management_vlan.has_value() && update.management_vlan->enabled &&
        !capabilities.supports_management_vlan) {
        add_error(result.errors, "management VLAN requested but platform capability is disabled");
    }
    if (update.management_vlan.has_value() && update.management_vlan->enabled &&
        capabilities.supports_management_vlan) {
        if (!update.management_vlan->vlan_id.has_value()) {
            add_error(result.errors, "enabled management VLAN is missing managementVlanId");
        }
        if (options.management_vlan_interface.empty() || options.management_vlan_device.empty()) {
            add_error(result.errors, "management VLAN interface and device are required");
        }
    }

    result.ok = result.errors.empty();
    return result;
}

UciPlan build_uci_plan(
    const application::AccessPointConfigUpdate& update,
    const platform::PlatformCapabilities& capabilities,
    const UciPlanOptions& options
) {
    UciPlan plan;
    const auto validation = validate_update(update, capabilities, options);
    if (!validation.ok) {
        plan.errors = validation.errors;
        return plan;
    }

    const auto active = active_wlans(update, capabilities);
    if (active.size() < update.wlans.size()) {
        plan.warning = "controller requested " + std::to_string(update.wlans.size()) +
            " SSIDs but platform max is " + std::to_string(capabilities.max_ssids) +
            "; applied first " + std::to_string(active.size());
    }

    std::vector<std::string> network_lines;
    std::set<std::uint16_t> ssid_vlan_ids;
    for (const auto& wlan : active) {
        if (wlan.vlan.vlan_id.has_value() && capabilities.supports_ssid_vlan) {
            ssid_vlan_ids.insert(*wlan.vlan.vlan_id);
        }
    }
    for (std::uint16_t vlan_id : ssid_vlan_ids) {
        auto lines = ssid_vlan_lines(vlan_id);
        network_lines.insert(network_lines.end(), lines.begin(), lines.end());
    }
    if (update.management_vlan.has_value()) {
        auto lines = management_vlan_lines(*update.management_vlan, options);
        network_lines.insert(network_lines.end(), lines.begin(), lines.end());
    }
    if (!network_lines.empty()) {
        plan.commands.insert(plan.commands.end(), network_lines.begin(), network_lines.end());
        plan.commands.push_back("commit network");
    }

    for (const auto& radio : update.radios) {
        auto lines = radio_lines(radio);
        plan.commands.insert(plan.commands.end(), lines.begin(), lines.end());
    }

    std::vector<std::string> default_sections;
    for (const auto& wlan : active) {
        append_unique(default_sections, "default_" + radio_section(wlan.band, wlan.radio_id));
    }
    for (const auto& section : default_sections) {
        plan.commands.push_back(set_line("wireless", section, "disabled", "1"));
    }

    for (std::size_t index = active.size(); index < update.wlans.size(); ++index) {
        plan.commands.push_back("delete wireless." + wlan_section(update.wlans[index]));
    }
    for (const auto& wlan : active) {
        auto lines = wlan_lines(wlan, capabilities);
        plan.commands.insert(plan.commands.end(), lines.begin(), lines.end());
    }

    if (!plan.commands.empty()) {
        plan.commands.push_back("commit wireless");
    }
    plan.ok = true;
    plan.changed = !plan.commands.empty();
    return plan;
}

std::string render_uci_batch(const std::vector<std::string>& commands) {
    std::string out;
    for (const auto& command : commands) {
        out += command;
        out.push_back('\n');
    }
    return out;
}

} // namespace openomada::openwrt
