#include <cstdlib>
#include <iostream>
#include <string>

#include "openomada/application/configuration.hpp"
#include "openomada/openwrt/uci.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

openomada::platform::PlatformCapabilities caps() {
    openomada::platform::PlatformCapabilities capabilities;
    capabilities.platform = "openwrt";
    capabilities.tools.uci = true;
    capabilities.tools.ubus = true;
    capabilities.radio_bands = {
        openomada::application::RadioBand::TwoG,
        openomada::application::RadioBand::FiveG,
    };
    capabilities.max_ssids = 4;
    capabilities.supports_wlan_config = true;
    capabilities.supports_wpa2_psk = true;
    return capabilities;
}

bool contains(const std::vector<std::string>& commands, const std::string& expected) {
    for (const auto& command : commands) {
        if (command == expected) {
            return true;
        }
    }
    return false;
}

void test_builds_idempotent_uci_batch_for_radio_and_psk_wlan() {
    const auto parsed = openomada::application::parse_config_body_json(R"({
        "wirelessBasic_2G": {
            "radioId": 0,
            "radioEnable": true,
            "channel": 11,
            "chanWidth": 20,
            "txPower": 14
        },
        "ssid_2G": {
            "radioId": 0,
            "ssid": [{
                "index": 1,
                "ssidName": "Open Omada",
                "ssidBcast": true,
                "ssidIsolation": false,
                "pskVer": 2,
                "pskKey": "network-passphrase"
            }]
        }
    })");
    require(parsed.ok, parsed.error.c_str());

    const auto plan = openomada::openwrt::build_uci_plan(parsed.update, caps());

    require(plan.ok, "plan ok");
    require(plan.changed, "plan changed");
    require(contains(plan.commands, "set wireless.radio0.disabled='0'"), "radio enable");
    require(contains(plan.commands, "set wireless.radio0.channel='11'"), "radio channel");
    require(contains(plan.commands, "set wireless.radio0.htmode='HT20'"), "radio htmode");
    require(contains(plan.commands, "set wireless.default_radio0.disabled='1'"), "default disabled");
    require(contains(plan.commands, "delete wireless.openomada_2g_1"), "delete managed wlan");
    require(contains(plan.commands, "set wireless.openomada_2g_1=wifi-iface"), "wifi-iface");
    require(contains(plan.commands, "set wireless.openomada_2g_1.ssid='Open Omada'"), "ssid");
    require(contains(plan.commands, "set wireless.openomada_2g_1.encryption='psk2'"), "psk2");
    require(contains(plan.commands, "set wireless.openomada_2g_1.key='network-passphrase'"), "psk key");
    require(plan.commands.back() == "commit wireless", "commit wireless");
}

void test_caps_requested_ssids_and_deletes_omitted_managed_sections() {
    const auto parsed = openomada::application::parse_config_body_json(R"({
        "ssid_2G": {
            "radioId": 0,
            "ssid": [
                {"index": 1, "ssidName": "guest"},
                {"index": 2, "ssidName": "corp", "pskKey": "secret"}
            ]
        }
    })");
    require(parsed.ok, parsed.error.c_str());
    auto limited = caps();
    limited.max_ssids = 1;

    const auto plan = openomada::openwrt::build_uci_plan(parsed.update, limited);

    require(plan.ok, "limited plan ok");
    require(plan.warning.find("controller requested 2 SSIDs") != std::string::npos, "capacity warning");
    require(contains(plan.commands, "set wireless.openomada_2g_1.ssid='guest'"), "first SSID applied");
    require(contains(plan.commands, "delete wireless.openomada_2g_2"), "second SSID deleted");
    require(!contains(plan.commands, "set wireless.openomada_2g_2.ssid='corp'"), "second SSID not applied");
}

void test_rejects_vlan_when_capability_is_disabled_and_builds_when_enabled() {
    const auto parsed = openomada::application::parse_config_body_json(
        R"({"ssid_2G":{"radioId":0,"ssid":[{"ssidName":"lab","vlanId":30}]}})"
    );
    require(parsed.ok, parsed.error.c_str());

    const auto rejected = openomada::openwrt::build_uci_plan(parsed.update, caps());
    require(!rejected.ok, "vlan rejected");
    require(!rejected.errors.empty(), "vlan error exists");
    require(rejected.errors[0].find("SSID VLAN requested") != std::string::npos, "vlan error message");

    auto vlan_caps = caps();
    vlan_caps.supports_ssid_vlan = true;
    const auto accepted = openomada::openwrt::build_uci_plan(parsed.update, vlan_caps);

    require(accepted.ok, "vlan accepted");
    require(contains(accepted.commands, "delete network.openomada_vlan30"), "network delete");
    require(contains(accepted.commands, "set network.openomada_vlan30=interface"), "network interface");
    require(contains(accepted.commands, "set network.openomada_vlan30.device='br-lan.30'"), "network device");
    require(contains(accepted.commands, "commit network"), "network commit");
    require(contains(accepted.commands, "set wireless.openomada_2g_lab.network='openomada_vlan30'"), "wlan network");
}

void test_management_vlan_requires_target_and_maps_device() {
    const auto parsed = openomada::application::parse_config_body_json(
        R"({"managementVlan":{"managementVlanEnable":"on","managementVlanId":99}})"
    );
    require(parsed.ok, parsed.error.c_str());
    auto management_caps = caps();
    management_caps.supports_management_vlan = true;

    const auto rejected = openomada::openwrt::build_uci_plan(parsed.update, management_caps);
    require(!rejected.ok, "management vlan without target rejected");

    openomada::openwrt::UciPlanOptions options;
    options.management_vlan_interface = "lan";
    options.management_vlan_device = "br-lan";
    const auto accepted = openomada::openwrt::build_uci_plan(parsed.update, management_caps, options);
    require(accepted.ok, "management vlan accepted");
    require(contains(accepted.commands, "set network.lan.device='br-lan.99'"), "management device");
    require(contains(accepted.commands, "commit network"), "management network commit");
}

void test_passive_portal_free_policy_has_no_uci_changes() {
    const auto parsed = openomada::application::parse_config_body_json(
        R"({"portalFreePolicyConfig":{"portalFreePolicy":[{"value":"192.0.2.10"}]}})"
    );
    require(parsed.ok, parsed.error.c_str());

    const auto plan = openomada::openwrt::build_uci_plan(parsed.update, caps());

    require(plan.ok, "passive plan ok");
    require(!plan.changed, "passive plan unchanged");
    require(plan.commands.empty(), "passive has no commands");
}

void test_portal_and_wpa3_require_capabilities() {
    const auto portal = openomada::application::parse_config_body_json(
        R"({"ssid_2G":{"radioId":0,"ssid":[{"ssidName":"guest","portal":true}]}})"
    );
    require(portal.ok, portal.error.c_str());
    require(!openomada::openwrt::build_uci_plan(portal.update, caps()).ok, "portal rejected");
    auto portal_caps = caps();
    portal_caps.supports_portal = true;
    require(openomada::openwrt::build_uci_plan(portal.update, portal_caps).ok, "portal accepted");

    const auto wpa3 = openomada::application::parse_config_body_json(
        R"({"ssid_2G":{"radioId":0,"ssid":[{"ssidName":"lab","pskVer":3,"pskKey":"secret"}]}})"
    );
    require(wpa3.ok, wpa3.error.c_str());
    require(!openomada::openwrt::build_uci_plan(wpa3.update, caps()).ok, "wpa3 rejected");
    auto wpa3_caps = caps();
    wpa3_caps.supports_wpa3_psk = true;
    require(openomada::openwrt::build_uci_plan(wpa3.update, wpa3_caps).ok, "wpa3 accepted");
}

void test_uci_escaping_handles_apostrophes_and_backslashes() {
    const auto parsed = openomada::application::parse_config_body_json(
        R"({"ssid_2G":{"ssid":[{"ssidName":"Bob's WiFi\\Lab","pskKey":"pa'ss\\word"}]}})"
    );
    require(parsed.ok, parsed.error.c_str());

    const auto plan = openomada::openwrt::build_uci_plan(parsed.update, caps());
    const std::string batch = openomada::openwrt::render_uci_batch(plan.commands);

    require(batch.find("Bob'\\''s WiFi\\\\Lab") != std::string::npos, "SSID escaped");
    require(batch.find("pa'\\''ss\\\\word") != std::string::npos, "PSK escaped");
}

} // namespace

int main() {
    test_builds_idempotent_uci_batch_for_radio_and_psk_wlan();
    test_caps_requested_ssids_and_deletes_omitted_managed_sections();
    test_rejects_vlan_when_capability_is_disabled_and_builds_when_enabled();
    test_management_vlan_requires_target_and_maps_device();
    test_passive_portal_free_policy_has_no_uci_changes();
    test_portal_and_wpa3_require_capabilities();
    test_uci_escaping_handles_apostrophes_and_backslashes();
    std::cout << "openomada-openwrt-uci-tests passed\n";
    return 0;
}
