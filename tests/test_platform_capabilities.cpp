#include <cstdlib>
#include <iostream>
#include <string>

#include "openomada/platform/capabilities.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_detects_openwrt_from_uci_and_ubus() {
    openomada::platform::PlatformCapabilityInput input;
    input.platform = "auto";
    input.radio_bands = "2g,5g";
    input.max_ssids = 8;
    input.tools.uci = true;
    input.tools.ubus = true;
    input.tools.nft = true;
    input.tools.hostapd = true;

    const auto result = openomada::platform::detect_platform_capabilities(input);

    require(result.ok, result.error.c_str());
    require(result.capabilities.platform == "openwrt", "auto detects openwrt");
    require(result.capabilities.tools.uci, "uci present");
    require(result.capabilities.tools.ubus, "ubus present");
    require(result.capabilities.tools.nft, "nft present");
    require(!result.capabilities.tools.dnsmasq, "dnsmasq absent");
    require(result.capabilities.radio_bands.size() == 2, "two bands");
    require(result.capabilities.radio_bands[0] == openomada::application::RadioBand::TwoG, "2g band");
    require(result.capabilities.radio_bands[1] == openomada::application::RadioBand::FiveG, "5g band");
    require(result.capabilities.max_ssids == 8, "max ssids from input");
    require(result.capabilities.supports_wlan_config, "wlan supported");
    require(result.capabilities.supports_wpa2_psk, "wpa2 supported");
    require(!result.capabilities.supports_portal, "portal not inferred without opennds");
    require(result.capabilities.supports_client_operations, "client operations with ubus");
}

void test_iw_no_interface_combinations_caps_at_one_ap() {
    openomada::platform::PlatformCapabilityInput input;
    input.platform = "openwrt";
    input.max_ssids = 4;
    input.tools.uci = true;
    input.tools.ubus = true;
    input.tools.iw = true;
    input.iw_list_output = R"(
Wiphy phy0
        Supported interface modes:
                 * managed
                 * AP
        interface combinations are not supported
)";

    const auto result = openomada::platform::detect_platform_capabilities(input);

    require(result.ok, result.error.c_str());
    require(result.capabilities.max_ssids == 1, "no interface combinations caps at one");
}

void test_iw_valid_combinations_caps_at_ap_group_limit() {
    openomada::platform::PlatformCapabilityInput input;
    input.platform = "openwrt";
    input.max_ssids = 8;
    input.tools.uci = true;
    input.tools.ubus = true;
    input.tools.iw = true;
    input.iw_list_output = R"(
valid interface combinations:
         * #{ managed } <= 1, #{ AP, mesh point } <= 2,
           total <= 3, #channels <= 1
)";

    const auto result = openomada::platform::detect_platform_capabilities(input);

    require(result.ok, result.error.c_str());
    require(result.capabilities.max_ssids == 2, "AP group limit");
}

void test_opennds_requires_opennds_and_ndsctl() {
    openomada::platform::PlatformCapabilityInput input;
    input.platform = "openwrt";
    input.tools.uci = true;
    input.tools.ubus = true;
    input.tools.opennds = true;
    input.tools.ndsctl = true;

    const auto result = openomada::platform::detect_platform_capabilities(input);

    require(result.ok, result.error.c_str());
    require(result.capabilities.tools.opennds, "openNDS engine present");
    require(result.capabilities.supports_portal, "portal supported");
    require(openomada::platform::capability_summary(result.capabilities).find("opennds:1") != std::string::npos, "summary opennds");
}

void test_capability_overrides_and_errors() {
    openomada::platform::PlatformCapabilityInput input;
    input.platform = "generic";
    input.cap_wlan = true;
    input.cap_dynamic_vlan = true;
    input.cap_portal = true;

    const auto result = openomada::platform::detect_platform_capabilities(input);

    require(result.ok, result.error.c_str());
    require(result.capabilities.platform == "generic", "generic platform");
    require(result.capabilities.supports_wlan_config, "override wlan");
    require(result.capabilities.supports_dynamic_vlan, "override dynamic vlan");
    require(result.capabilities.supports_portal, "override portal");

    input.platform = "linux";
    const auto bad_platform = openomada::platform::detect_platform_capabilities(input);
    require(!bad_platform.ok, "bad platform rejected");

    input.platform = "generic";
    input.radio_bands = "7g";
    const auto bad_band = openomada::platform::detect_platform_capabilities(input);
    require(!bad_band.ok, "bad band rejected");
}

void test_summary_is_secret_free_and_stable() {
    openomada::platform::PlatformCapabilityInput input;
    input.platform = "generic";
    input.cap_radius = true;
    const auto result = openomada::platform::detect_platform_capabilities(input);
    require(result.ok, result.error.c_str());

    const std::string summary = openomada::platform::capability_summary(result.capabilities);
    require(summary.find("platform=generic") != std::string::npos, "summary platform");
    require(summary.find("radius") != std::string::npos, "summary radius");
    require(summary.find("password") == std::string::npos, "summary no password");
    require(summary.find("secret") == std::string::npos, "summary no secret");
}

} // namespace

int main() {
    test_detects_openwrt_from_uci_and_ubus();
    test_iw_no_interface_combinations_caps_at_one_ap();
    test_iw_valid_combinations_caps_at_ap_group_limit();
    test_opennds_requires_opennds_and_ndsctl();
    test_capability_overrides_and_errors();
    test_summary_is_secret_free_and_stable();
    std::cout << "openomada-platform-capabilities-tests passed\n";
    return 0;
}
