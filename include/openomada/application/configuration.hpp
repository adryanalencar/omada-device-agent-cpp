#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openomada/domain/mac_address.hpp"

namespace openomada::application {

enum class RadioBand {
    TwoG,
    FiveG,
    FiveG2,
    SixG,
};

const char* to_wire_string(RadioBand band) noexcept;

struct DhcpOption82Intent {
    bool enabled{false};
    std::optional<std::int64_t> format{};
    std::string delimiter{};
    std::vector<std::int64_t> circuit_id{};
    std::vector<std::int64_t> remote_id{};
    std::string site_name{};
};

struct WirelessVlanIntent {
    std::optional<std::uint16_t> vlan_id{};
    std::vector<std::string> vlan_pool_ids{};
    std::optional<std::int64_t> dynamic_vlan_mode{};
    std::optional<DhcpOption82Intent> dhcp_option82{};
};

struct WirelessSecurity {
    std::optional<std::int64_t> security_mode{};
    std::optional<std::int64_t> auth_type{};
    std::optional<std::int64_t> wpa_version{};
    std::optional<std::int64_t> wpa_cipher{};
    std::optional<std::int64_t> psk_version{};
    std::optional<std::int64_t> psk_cipher{};
    bool psk_configured{false};
    std::string psk_key{};
    std::string radius_profile_id{};
    bool radius_auth{false};
    bool radius_accounting{false};
    bool radius_mac_auth{false};
    std::optional<std::int64_t> pmf_mode{};
    std::optional<bool> fast_roaming{};
};

struct CaptivePortalIntent {
    bool enabled{false};
    std::optional<bool> https_redirect{};
    bool hotspot_v2_present{false};
};

struct RadioConfig {
    RadioBand band{RadioBand::TwoG};
    std::optional<std::int64_t> radio_id{};
    std::optional<bool> enabled{};
    std::optional<std::int64_t> channel_width{};
    std::optional<std::int64_t> channel{};
    std::optional<std::int64_t> tx_power{};
    std::optional<bool> channel_limit{};
    std::optional<std::int64_t> wireless_mode{};
};

struct WirelessNetwork {
    RadioBand band{RadioBand::TwoG};
    std::optional<std::int64_t> radio_id{};
    std::optional<std::int64_t> ssid_id{};
    std::optional<std::int64_t> index{};
    std::optional<std::int64_t> operation{};
    std::string name{};
    std::optional<bool> broadcast{};
    std::optional<bool> client_isolation{};
    WirelessVlanIntent vlan{};
    WirelessSecurity security{};
    CaptivePortalIntent portal{};
};

struct ManagementVlan {
    bool enabled{false};
    std::optional<std::uint16_t> vlan_id{};
};

struct PortalFreePolicy {
    std::size_t layer2_rule_count{0};
    std::size_t url_rule_count{0};
};

struct PortalConfiguration {
    std::optional<std::int64_t> auth_type{};
    std::optional<std::int64_t> auth_timeout{};
    std::optional<std::int64_t> portal_day{};
    std::optional<std::int64_t> portal_hour{};
    std::optional<std::int64_t> portal_min{};
    std::optional<bool> https_redirect_enable{};
    std::optional<bool> redirect{};
    std::string redirect_url{};
    std::optional<std::int64_t> auth_server_type{};
    std::string ext_auth_server{};
    std::string external_portal_server{};
    std::string site_id{};
    std::string site_name{};
    std::string portal_title{};
    std::optional<bool> portal_accept{};
    std::vector<std::string> ssid_list{};
};

struct LedConfig {
    std::optional<bool> enabled{};
    std::optional<bool> locate{};
};

struct WifiControlLedConfig {
    std::optional<bool> enabled{};
    std::optional<bool> is_pressed{};
};

struct ClientAuthConfig {
    domain::MacAddress client_mac{};
    std::optional<bool> unauthenticated{};
};

struct ClientControlOperation {
    domain::MacAddress client_mac{};
    std::optional<std::int64_t> operation{};
    std::string ssid{};
    std::optional<std::int64_t> radio_id{};
    std::optional<std::int64_t> vid{};
    std::optional<std::int64_t> port{};
    std::optional<bool> wireless{};
    std::string source_key{"clientOperation"};
};

struct ClientRateLimit {
    domain::MacAddress mac{};
    std::optional<std::int64_t> down{};
    std::optional<std::int64_t> up{};
};

struct ClientRateConfig {
    std::optional<std::int64_t> action{};
    std::vector<ClientRateLimit> limits{};
};

struct AccessPointConfigUpdate {
    std::optional<std::int64_t> sequence_id{};
    std::optional<std::int64_t> config_version{};
    std::optional<std::int64_t> config_version_inc{};
    std::vector<RadioConfig> radios{};
    std::vector<WirelessNetwork> wlans{};
    std::optional<ManagementVlan> management_vlan{};
    std::optional<PortalFreePolicy> portal_free_policy{};
    std::vector<PortalConfiguration> portal_configs{};
    std::optional<LedConfig> led{};
    std::optional<WifiControlLedConfig> wifi_control_led{};
    std::vector<ClientAuthConfig> client_configs{};
    std::vector<ClientControlOperation> client_operations{};
    std::optional<ClientRateConfig> client_rate_config{};
    std::vector<std::string> passive_keys{};
    std::vector<std::string> ack_only_keys{};
    std::vector<std::string> unhandled_keys{};
};

struct ConfigParseResult {
    bool ok{false};
    AccessPointConfigUpdate update{};
    std::string error{};
};

ConfigParseResult parse_config_body_json(std::string_view body_json) noexcept;
ConfigParseResult parse_set_request_json(std::string_view message_json) noexcept;

std::string describe_config_update(const AccessPointConfigUpdate& update);

bool is_actionable_config(const AccessPointConfigUpdate& update) noexcept;
bool is_supported_config_update(const AccessPointConfigUpdate& update) noexcept;

} // namespace openomada::application
