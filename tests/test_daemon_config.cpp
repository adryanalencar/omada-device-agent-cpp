#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "openomada/application/daemon_config.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

openomada::application::RuntimeOptions options() {
    openomada::application::RuntimeOptions runtime;
    runtime.config_path = "/tmp/openomada-test";
    return runtime;
}

void test_parse_runtime_options() {
    const auto parsed = openomada::application::parse_runtime_options({
        "openomada-agent",
        "--config",
        "/etc/config/openomada-test",
        "--once",
        "--check-config",
        "--dry-run",
    });

    require(parsed.ok, parsed.error.c_str());
    require(parsed.options.config_path == "/etc/config/openomada-test", "config path");
    require(parsed.options.once, "once flag");
    require(parsed.options.check_config, "check flag");
    require(parsed.options.dry_run, "dry-run flag");
}

void test_loads_uci_config_and_never_exposes_password_in_summary() {
    const auto loaded = openomada::application::load_daemon_config_from_text(R"(
config controller 'main'
	option enabled '1'
	option host '192.0.2.10'
	option site_id 'ffff16a3ab739b57bd5247ec2ff8b'
	option username 'admin'
	option password 'top-secret'

config agent 'main'
	option state_path '/tmp/openomada-state.json'
	option inform_interval_ms '30000'
	option protocol_trace '1'
	option radio_bands '2g,5g'
	option max_ssids '8'

config device 'main'
	option mac '02-11-22-33-44-55'
	option name 'OpenWRT'
	option model 'EAP225'
	option firmware_version '5.1.0'

config openwrt 'main'
	option management_vlan_interface 'lan'
	option management_vlan_device 'br-lan'

config portal 'main'
	option enabled '1'
	option engine 'opennds'
	option flush_conntrack_on_deauth '0'
)",
        options()
    );

    require(loaded.ok, loaded.error.c_str());
    const auto& config = loaded.config;
    require(config.enabled, "enabled");
    require(config.settings.controller_host == "192.0.2.10", "host");
    require(config.settings.site_id == "ffff16a3ab739b57bd5247ec2ff8b", "site");
    require(config.settings.device_username == "admin", "username");
    require(config.settings.device_password == "top-secret", "password loaded");
    require(config.settings.mac.normalized() == "02:11:22:33:44:55", "mac normalized");
    require(config.settings.device_name == "OpenWRT", "device name");
    require(config.settings.model == "EAP225", "model");
    require(config.settings.firmware_version == "5.1.0", "firmware");
    require(config.settings.state_file == "/tmp/openomada-state.json", "state");
    require(config.settings.inform_interval_ms == 30000, "inform interval");
    require(config.protocol_trace, "protocol trace");
    require(config.openwrt.radio_bands == "2g,5g", "bands");
    require(config.openwrt.max_ssids == 8, "max ssids");
    require(config.openwrt.management_vlan_interface == "lan", "mgmt vlan iface");
    require(config.openwrt.management_vlan_device == "br-lan", "mgmt vlan device");
    require(config.openwrt.portal_enabled, "portal enabled");
    require(config.openwrt.portal_engine == "opennds", "portal engine");
    require(!config.openwrt.flush_conntrack_on_deauth, "conntrack flush override");

    const std::string summary = openomada::application::daemon_config_summary(config);
    require(summary.find("top-secret") == std::string::npos, "summary omits password");
    require(summary.find("02-11-22-33-44-55") != std::string::npos, "summary includes Omada MAC");
}

void test_environment_overrides_uci_values() {
    const auto loaded = openomada::application::load_daemon_config_from_text(R"(
config controller 'main'
	option enabled '1'
	option host '192.0.2.10'
	option username 'admin'
	option password 'old-secret'

config device 'main'
	option mac '02:11:22:33:44:55'
)",
        options(),
        {
            {"OPENOMADA_CONTROLLER_HOST", "198.51.100.20"},
            {"OPENOMADA_DEVICE_PASSWORD", "new-secret"},
            {"OPENOMADA_DEVICE_MAC", "AA-BB-CC-DD-EE-FF"},
            {"OPENOMADA_INFORM_INTERVAL_MS", "45000"},
            {"OPENOMADA_PORTAL_ENABLED", "0"},
        }
    );

    require(loaded.ok, loaded.error.c_str());
    require(loaded.config.settings.controller_host == "198.51.100.20", "env host");
    require(loaded.config.settings.device_password == "new-secret", "env password");
    require(loaded.config.settings.mac.normalized() == "aa:bb:cc:dd:ee:ff", "env mac");
    require(loaded.config.settings.inform_interval_ms == 45000, "env interval");
    require(!loaded.config.openwrt.portal_enabled, "env portal");
}

void test_disabled_config_allows_empty_credentials() {
    const auto loaded = openomada::application::load_daemon_config_from_text(R"(
config controller 'main'
	option enabled '0'

config device 'main'
	option name 'OpenOmada-AP'
)",
        options()
    );

    require(loaded.ok, loaded.error.c_str());
    require(!loaded.config.enabled, "disabled");
}

void test_enabled_config_requires_runtime_identity_and_password() {
    const auto missing_password = openomada::application::load_daemon_config_from_text(R"(
config controller 'main'
	option enabled '1'
	option host '192.0.2.10'

config device 'main'
	option mac '02:11:22:33:44:55'
)",
        options()
    );
    require(!missing_password.ok, "missing password rejected");
    require(missing_password.error.find("password") != std::string::npos, "password error");

    const auto missing_mac = openomada::application::load_daemon_config_from_text(R"(
config controller 'main'
	option enabled '1'
	option host '192.0.2.10'
	option password 'secret'
)",
        options()
    );
    require(!missing_mac.ok, "missing mac rejected");
    require(missing_mac.error.find("MAC") != std::string::npos, "mac error");
}

} // namespace

int main() {
    test_parse_runtime_options();
    test_loads_uci_config_and_never_exposes_password_in_summary();
    test_environment_overrides_uci_values();
    test_disabled_config_allows_empty_credentials();
    test_enabled_config_requires_runtime_identity_and_password();
    std::cout << "openomada-daemon-config-tests passed\n";
    return 0;
}
