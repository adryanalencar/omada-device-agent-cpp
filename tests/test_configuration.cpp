#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>

#include "openomada/application/configuration_applier.hpp"
#include "openomada/application/configuration.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class RecordingApplier final : public openomada::application::ConfigurationApplier {
public:
    openomada::application::ConfigurationApplyResult result{true, false, {}};
    std::size_t calls{0};

    openomada::application::ConfigurationApplyResult apply(
        const openomada::application::AccessPointConfigUpdate& update
    ) override {
        (void)update;
        ++calls;
        return result;
    }
};

void test_parse_radio_wlan_vlan_portal_and_led_config() {
    const auto parsed = openomada::application::parse_config_body_json(R"({
        "sequenceId": 42,
        "configVersion": 7,
        "wirelessBasic_2G": {
            "radioId": 1,
            "radioEnable": true,
            "chanWidth": 20,
            "channel": 6,
            "txPower": 12,
            "channelLimit": false
        },
        "ssid_2G": {
            "radioId": 1,
            "ssid": [{
                "id": 100,
                "index": 1,
                "operation": 1,
                "ssidName": "lab-wlan",
                "ssidBcast": false,
                "ssidIsolation": true,
                "vlanId": 30,
                "securityMode": 3,
                "pskVer": 2,
                "pskCipher": 1,
                "pskKey": "secret-value-is-not-copied-to-a-field",
                "portal": true,
                "httpsRedirectEnable": true,
                "dyVlanMode": 2,
                "dhcpOp82": {
                    "option82En": true,
                    "option82Format": 1,
                    "delimiter": ":",
                    "circuitId": [1, 2],
                    "remoteId": [3],
                    "siteName": "HQ"
                },
                "fastTransition": {"enable11r": true}
            }]
        },
        "managementVlan": {
            "managementVlanEnable": "on",
            "managementVlanId": 99
        },
        "portalFreePolicyConfig": {
            "portalFreePolicy": [{"type": "ip", "value": "192.0.2.10"}],
            "urlPortalFreePolicy": [{"host": "example.com"}]
        },
        "portalConfigList": [{
            "authType": 4,
            "authTimeout": 120,
            "httpsRedirectEnable": false,
            "redirect": true,
            "redirectUrl": "https://example.com/landing",
            "authServerType": 2,
            "externalPortalServer": "https://portal.example.com/login",
            "siteName": "HQ",
            "portalTitle": "Guest Portal",
            "portalAccept": true,
            "ssidList": ["lab-wlan"],
            "password": "do-not-copy",
            "radiusPassword": "do-not-copy"
        }],
        "led": {"enable": "on"}
    })");

    require(parsed.ok, parsed.error.c_str());
    const auto& update = parsed.update;
    require(update.sequence_id.value_or(0) == 42, "sequenceId parsed");
    require(update.config_version.value_or(0) == 7, "configVersion parsed");
    require(update.unhandled_keys.empty(), "no unhandled keys");
    require(update.led.has_value(), "LED parsed");
    require(update.led->enabled.value_or(false), "LED enable parsed");
    require(!update.led->locate.has_value(), "LED locate absent");

    require(update.radios.size() == 1, "one radio");
    require(update.radios[0].band == openomada::application::RadioBand::TwoG, "2g radio");
    require(update.radios[0].radio_id.value_or(0) == 1, "radio id");
    require(update.radios[0].enabled.value_or(false), "radio enabled");
    require(update.radios[0].channel.value_or(0) == 6, "channel");
    require(update.radios[0].channel_width.value_or(0) == 20, "channel width");
    require(update.radios[0].tx_power.value_or(0) == 12, "tx power");

    require(update.wlans.size() == 1, "one WLAN");
    const auto& wlan = update.wlans[0];
    require(wlan.name == "lab-wlan", "SSID name");
    require(!wlan.broadcast.value_or(true), "SSID broadcast false");
    require(wlan.client_isolation.value_or(false), "SSID isolation true");
    require(wlan.vlan.vlan_id.value_or(0) == 30, "SSID VLAN");
    require(wlan.vlan.dynamic_vlan_mode.value_or(0) == 2, "dynamic VLAN");
    require(wlan.vlan.dhcp_option82.has_value(), "DHCP option82 parsed");
    require(wlan.vlan.dhcp_option82->enabled, "DHCP option82 enabled");
    require(wlan.vlan.dhcp_option82->circuit_id.size() == 2, "DHCP option82 circuit id");
    require(wlan.security.psk_configured, "PSK configured flag");
    require(wlan.security.fast_roaming.value_or(false), "fast roaming");
    require(wlan.portal.enabled, "portal enabled");

    require(update.management_vlan.has_value(), "management VLAN parsed");
    require(update.management_vlan->enabled, "management VLAN enabled");
    require(update.management_vlan->vlan_id.value_or(0) == 99, "management VLAN ID");
    require(update.portal_free_policy.has_value(), "portal free policy parsed");
    require(update.portal_free_policy->layer2_rule_count == 1, "portal L2 free policy count");
    require(update.portal_free_policy->url_rule_count == 1, "portal URL free policy count");
    require(update.portal_free_policy->layer2_rules.size() == 1, "portal L2 rule retained");
    require(update.portal_free_policy->url_rules.size() == 1, "portal URL rule retained");
    require(update.portal_free_policy->url_rules[0].host == "example.com", "portal URL host retained");
    require(update.portal_configs.size() == 1, "portal config parsed");
    require(update.portal_configs[0].auth_type.value_or(0) == 4, "portal auth type");
    require(update.portal_configs[0].external_portal_server == "https://portal.example.com/login", "external portal server");
    require(update.portal_configs[0].redirect_url == "https://example.com/landing", "redirect url");
    require(update.portal_configs[0].site_name == "HQ", "portal site name");
    require(update.portal_configs[0].ssid_list.size() == 1, "portal ssid list");
}

void test_parse_client_config_operations_and_rate_limits() {
    const auto parsed = openomada::application::parse_config_body_json(R"({
        "clientConfig": [{"clientMac": "AA-BB-CC-DD-EE-FF", "unauth": true}],
        "clientOperation_cmd": [{"clientMac": "AA-BB-CC-DD-EE-FF", "operation": 2}],
        "clientOperation": [{
            "clientMac": "02:00:00:00:00:02",
            "operation": 3,
            "ssid": "guest",
            "radioId": 1
        }],
        "clientRateConfig": {
            "action": 0,
            "clientRateLimit": [{"mac": "AA-BB-CC-DD-EE-FF", "down": 1024, "up": 512}]
        }
    })");

    require(parsed.ok, parsed.error.c_str());
    const auto& update = parsed.update;
    require(update.client_configs.size() == 1, "clientConfig parsed");
    require(update.client_configs[0].client_mac.normalized() == "aa:bb:cc:dd:ee:ff", "clientConfig MAC normalized");
    require(update.client_configs[0].unauthenticated.value_or(false), "clientConfig unauth");
    require(update.client_operations.size() == 2, "client operations parsed");
    require(update.client_operations[0].source_key == "clientOperation", "clientOperation order");
    require(update.client_operations[0].operation.value_or(0) == 3, "clientOperation operation");
    require(update.client_operations[0].ssid == "guest", "clientOperation ssid");
    require(update.client_operations[1].source_key == "clientOperation_cmd", "clientOperation_cmd order");
    require(update.client_operations[1].operation.value_or(0) == 2, "clientOperation_cmd operation");
    require(update.client_rate_config.has_value(), "client rate config parsed");
    require(update.client_rate_config->action.value_or(-1) == 0, "client rate action");
    require(update.client_rate_config->limits.size() == 1, "client rate limit parsed");
    require(update.client_rate_config->limits[0].mac.normalized() == "aa:bb:cc:dd:ee:ff", "rate MAC normalized");
}

void test_parser_rejects_invalid_vlan_ssid_and_missing_client_mac() {
    auto invalid_vlan = openomada::application::parse_config_body_json(
        R"({"ssid_5G":{"ssid":[{"ssidName":"bad","vlanId":5000}]}})"
    );
    require(!invalid_vlan.ok, "invalid VLAN rejected");
    require(invalid_vlan.error.find("VLAN ID") != std::string::npos, "invalid VLAN error");

    auto long_ssid = openomada::application::parse_config_body_json(
        R"({"ssid_2G":{"ssid":[{"ssidName":"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"}]}})"
    );
    require(!long_ssid.ok, "long SSID rejected");
    require(long_ssid.error.find("32 UTF-8 bytes") != std::string::npos, "long SSID error");

    auto missing_mac = openomada::application::parse_config_body_json(
        R"({"clientOperation":[{"operation":2}]})"
    );
    require(!missing_mac.ok, "missing client MAC rejected");
    require(missing_mac.error.find("clientOperation.clientMac") != std::string::npos, "missing MAC error");
}

void test_passive_and_unhandled_keys_are_classified() {
    const auto passive = openomada::application::parse_config_body_json(R"({
        "lanSetting": {"connType": 1},
        "macFilterGlobal": {"enable": true},
        "schedulerGlobal": {"enable": true},
        "logSetting": {"mailEnable": false},
        "ssh": {"sshenable": "on"},
        "ipGroup": {"ipGroups": []},
        "ipv6Group": {"ipv6Groups": []},
        "snmp": {"v1v2cEnable": 0},
        "lldp": {"enable": 1},
        "wirelessAdv_2G": {"radioId": 0}
    })");
    require(passive.ok, passive.error.c_str());
    require(passive.update.unhandled_keys.empty(), "passive defaults not unhandled");
    require(passive.update.passive_keys.size() == 10, "passive keys counted");
    require(passive.update.passive_keys.front() == "ipGroup", "passive keys sorted");

    const auto unsupported = openomada::application::parse_config_body_json(
        R"({"unsupportedCommand":{"enabled":true}})"
    );
    require(unsupported.ok, unsupported.error.c_str());
    require(unsupported.update.unhandled_keys.size() == 1, "unsupported classified");
    require(!openomada::application::is_supported_config_update(unsupported.update), "unsupported update rejected by policy");
}

void test_describe_config_update_omits_secrets() {
    const auto parsed = openomada::application::parse_config_body_json(R"({
        "sequenceId": 10,
        "configVersionInc": 1,
        "wirelessBasic_2G": {"radioId": 1, "radioEnable": true},
        "ssid_2G": {"radioId": 1, "ssid": [{"ssidName": "private", "pskKey": "do-not-log"}]},
        "managementVlan": {"managementVlanEnable": "on", "managementVlanId": 20},
        "portalFreePolicyConfig": {"portalFreePolicy": [{}], "urlPortalFreePolicy": [{}, {}]}
    })");
    require(parsed.ok, parsed.error.c_str());
    const std::string description = openomada::application::describe_config_update(parsed.update);
    require(description.find("sequenceId=10") != std::string::npos, "description sequence");
    require(description.find("radios=1[2g]") != std::string::npos, "description radios");
    require(description.find("wlans=1[2g]") != std::string::npos, "description wlans");
    require(description.find("managementVlan=on:20") != std::string::npos, "description management VLAN");
    require(description.find("portalFreePolicy=l2:1,url:2") != std::string::npos, "description portal policy");
    require(description.find("private") == std::string::npos, "SSID name omitted from description");
    require(description.find("do-not-log") == std::string::npos, "PSK omitted from description");
}

void test_composite_configuration_applier_routes_to_all_adapters() {
    RecordingApplier first;
    first.result = {true, true, {}};
    RecordingApplier second;
    second.result = {true, false, {}};
    openomada::application::CompositeConfigurationApplier composite({&first});
    composite.add(second);

    const auto result = composite.apply(openomada::application::AccessPointConfigUpdate{});

    require(result.ok, result.error.c_str());
    require(result.changed, "composite changed if any adapter changed");
    require(first.calls == 1, "first applier called");
    require(second.calls == 1, "second applier called");

    second.result = {false, false, "second failed"};
    const auto failure = composite.apply(openomada::application::AccessPointConfigUpdate{});
    require(!failure.ok, "composite failure propagated");
    require(failure.error == "second failed", "composite error propagated");
}

} // namespace

int main() {
    test_parse_radio_wlan_vlan_portal_and_led_config();
    test_parse_client_config_operations_and_rate_limits();
    test_parser_rejects_invalid_vlan_ssid_and_missing_client_mac();
    test_passive_and_unhandled_keys_are_classified();
    test_describe_config_update_omits_secrets();
    test_composite_configuration_applier_routes_to_all_adapters();
    std::cout << "openomada-configuration-tests passed\n";
    return 0;
}
