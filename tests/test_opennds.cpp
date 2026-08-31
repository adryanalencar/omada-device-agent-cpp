#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "openomada/application/configuration.hpp"
#include "openomada/domain/mac_address.hpp"
#include "openomada/openwrt/opennds.hpp"
#include "openomada/protocol/json.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

openomada::domain::MacAddress mac(const char* value) {
    auto parsed = openomada::domain::MacAddress::parse(value);
    require(parsed.has_value(), "MAC parses");
    return *parsed;
}

bool contains(const std::vector<std::string>& values, const std::string& expected) {
    return std::find(values.begin(), values.end(), expected) != values.end();
}

bool command_exists(const std::vector<std::vector<std::string>>& commands, const std::vector<std::string>& expected) {
    return std::find(commands.begin(), commands.end(), expected) != commands.end();
}

void test_builds_opennds_policy_from_portal_free_policy() {
    const auto parsed = openomada::application::parse_config_body_json(R"({
        "portalFreePolicyConfig": {
            "portalFreePolicy": [
                {"dstIp": "8.8.8.8", "dstMask": 32},
                {"value": "192.0.2.0", "mask": 24},
                {"value": "not-an-ip"}
            ],
            "urlPortalFreePolicy": [
                {"url": "mediabeach.com.br/portal/c00e9a43"},
                {"url": "https://privacy.tp-link.com/path"},
                {"url": "192.0.2.20"}
            ]
        }
    })");
    require(parsed.ok, parsed.error.c_str());

    const auto policy = openomada::openwrt::opennds_portal_policy_from_free_policy(parsed.update.portal_free_policy);

    require(contains(policy.walled_garden_fqdns, "mediabeach.com.br"), "portal host");
    require(contains(policy.walled_garden_fqdns, "privacy.tp-link.com"), "privacy host");
    require(!contains(policy.walled_garden_fqdns, "192.0.2.20"), "IP host omitted from FQDN list");
    require(contains(policy.preauthenticated_user_rules, "allow all to 8.8.8.8/32"), "single IP rule");
    require(contains(policy.preauthenticated_user_rules, "allow all to 192.0.2.0/24"), "network rule");
}

void test_builds_opennds_policy_with_external_portal_redirect() {
    const auto parsed = openomada::application::parse_config_body_json(R"({
        "portalConfigList": [{
            "externalPortalServer": "https://portal.example.com/login",
            "redirectUrl": "https://example.com/after-login",
            "siteId": "ffff16a3ab739b57bd5247ec2ff8b",
            "siteName": "Pereque Mirim",
            "ssidList": ["Ubatuba - Wifi Grátis"]
        }]
    })");
    require(parsed.ok, parsed.error.c_str());

    const auto policy = openomada::openwrt::opennds_portal_policy_from_omada_config(
        parsed.update,
        "192.0.2.1",
        mac("02:11:22:33:44:55")
    );

    require(policy.portal_redirect_url == "https://portal.example.com/login", "external portal redirect");
    require(policy.landing_page_url == "https://example.com/after-login", "landing URL");
    require(policy.default_ssid_name == "Ubatuba - Wifi Grátis", "SSID");
    require(policy.ap_mac == "02-11-22-33-44-55", "AP MAC");
    require(policy.site_id == "ffff16a3ab739b57bd5247ec2ff8b", "site id");
    require(policy.site_name == "Pereque Mirim", "site name");
}

void test_builds_opennds_policy_from_free_policy_portal_url_and_controller_fallback() {
    const auto free_policy = openomada::application::parse_config_body_json(R"({
        "portalFreePolicyConfig": {
            "urlPortalFreePolicy": [
                {"url": "mediabeach.com.br"},
                {"url": "mediabeach.com.br/portal/c00e9a43"}
            ]
        }
    })");
    require(free_policy.ok, free_policy.error.c_str());
    auto policy = openomada::openwrt::opennds_portal_policy_from_omada_config(
        free_policy.update,
        "192.0.2.1",
        mac("02-11-22-33-44-55"),
        {},
        "Pereque Mirim"
    );
    require(policy.portal_redirect_url == "https://mediabeach.com.br/portal/c00e9a43", "portal URL from free policy");
    require(policy.site_name == "Pereque Mirim", "configured site name");

    const auto empty = openomada::application::parse_config_body_json("{}");
    require(empty.ok, empty.error.c_str());
    policy = openomada::openwrt::opennds_portal_policy_from_omada_config(
        empty.update,
        "omada.example.net",
        mac("02-11-22-33-44-55")
    );
    require(policy.portal_redirect_url == "http://omada.example.net:8088/portal/entry", "controller fallback");
}

void test_builds_themespec_with_omada_external_portal_parameters() {
    const auto parsed = openomada::application::parse_config_body_json(R"({
        "portalFreePolicyConfig": {
            "urlPortalFreePolicy": [{"url": "mediabeach.com.br/portal/c00e9a43?x=1&y=2"}]
        },
        "portalConfigList": [{
            "redirectUrl": "https://example.com/after-login",
            "siteId": "ffff16a3ab739b57bd5247ec2ff8b",
            "siteName": "Pereque Mirim",
            "ssidList": ["Ubatuba - Wifi Grátis"]
        }]
    })");
    require(parsed.ok, parsed.error.c_str());
    const auto policy = openomada::openwrt::opennds_portal_policy_from_omada_config(
        parsed.update,
        "",
        mac("02:11:22:33:44:55")
    );

    const std::string script = openomada::openwrt::build_openomada_redirect_themespec(policy);

    require(script.find("openomada_portal_url='https://mediabeach.com.br/portal/c00e9a43?x=1&y=2'") != std::string::npos, "quoted portal URL");
    require(script.find("clientMac") != std::string::npos, "clientMac");
    require(script.find("clientIp") != std::string::npos, "clientIp");
    require(script.find("apMac") != std::string::npos, "apMac");
    require(script.find("ssidName") != std::string::npos, "ssidName");
    require(script.find("radioId") != std::string::npos, "radioId");
    require(script.find("site") != std::string::npos, "site");
    require(script.find("redirectUrl") != std::string::npos, "redirectUrl");
    require(script.find("meta http-equiv") != std::string::npos, "refresh");
    require(script.find("https://mediabeach.com.br/portal/c00e9a43?x=1&amp;y=2") != std::string::npos, "HTML escaped comment");
}

void test_builds_opennds_apply_plan_with_gatewayfqdn_disabled() {
    const auto parsed = openomada::application::parse_config_body_json(R"({
        "portalFreePolicyConfig": {
            "portalFreePolicy": [{"dstIp": "8.8.8.8", "dstMask": 32}],
            "urlPortalFreePolicy": [{"url": "mediabeach.com.br/portal/c00e9a43"}]
        }
    })");
    require(parsed.ok, parsed.error.c_str());
    const auto policy = openomada::openwrt::opennds_portal_policy_from_omada_config(
        parsed.update,
        "",
        mac("02:11:22:33:44:55")
    );

    const auto plan = openomada::openwrt::build_opennds_apply_plan(policy);

    require(plan.ok, plan.error.c_str());
    require(plan.changed, "plan changed");
    require(!plan.themespec.empty(), "ThemeSpec generated");
    require(command_exists(plan.commands, {"uci", "-q", "delete", "opennds.@opennds[0].walledgarden_fqdn_list"}), "delete fqdn list");
    require(command_exists(plan.commands, {"uci", "add_list", "opennds.@opennds[0].walledgarden_fqdn_list=mediabeach.com.br"}), "add fqdn");
    require(command_exists(plan.commands, {"uci", "add_list", "opennds.@opennds[0].walledgarden_port_list=80 443 8088 8843"}), "add ports");
    require(command_exists(plan.commands, {"uci", "add_list", "opennds.@opennds[0].preauthenticated_users=allow all to 8.8.8.8/32"}), "add preauth");
    require(command_exists(plan.commands, {"write-file", openomada::openwrt::kOpenOmadaThemeSpecPath}), "write themespec");
    require(command_exists(plan.commands, {"uci", "set", "opennds.@opennds[0].login_option_enabled=3"}), "login option");
    require(command_exists(plan.commands, {"uci", "set", "opennds.@opennds[0].gatewayfqdn=disable"}), "disable gateway fqdn");
    require(command_exists(plan.commands, {"uci", "set", "opennds.@opennds[0].allow_preemptive_authentication=0"}), "disable preemptive auth");
    require(command_exists(plan.commands, {"/etc/init.d/opennds", "restart"}), "restart opennds");
}

void test_maps_opennds_json_clients_to_portal_overlay_state() {
    const auto result = openomada::openwrt::opennds_clients_from_json(R"({
        "clients": {
            "AA-BB-CC-DD-EE-FF": {
                "ip": "192.0.2.10",
                "state": "Authenticated",
                "download_this_session": "1234",
                "upload_this_session": "567"
            },
            "02:00:00:00:00:02": {
                "state": "Preauthenticated"
            }
        }
    })");

    require(result.ok, result.error.c_str());
    require(result.clients.size() == 2, "two openNDS clients");
    require(result.clients[0].mac.normalized() == "02:00:00:00:00:02", "first sorted client");
    require(result.clients[0].portal_state == openomada::application::ClientPortalState::Unauthenticated, "preauth state");
    require(result.clients[1].mac.normalized() == "aa:bb:cc:dd:ee:ff", "second sorted client");
    require(result.clients[1].ipv4 == "192.0.2.10", "IP");
    require(result.clients[1].portal_state == openomada::application::ClientPortalState::Authenticated, "auth state");
    require(result.clients[1].rx_bytes == 1234, "download counter");
    require(result.clients[1].tx_bytes == 567, "upload counter");
}

} // namespace

int main() {
    test_builds_opennds_policy_from_portal_free_policy();
    test_builds_opennds_policy_with_external_portal_redirect();
    test_builds_opennds_policy_from_free_policy_portal_url_and_controller_fallback();
    test_builds_themespec_with_omada_external_portal_parameters();
    test_builds_opennds_apply_plan_with_gatewayfqdn_disabled();
    test_maps_opennds_json_clients_to_portal_overlay_state();
    std::cout << "openomada-opennds-tests passed\n";
    return 0;
}
