#include "openomada/application/configuration.hpp"

#include "openomada/protocol/json.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <json-c/json.h>
#include <set>
#include <string>
#include <utility>

namespace openomada::application {
namespace {

const std::set<std::string> kCommonKeys{
    "sequenceId",
    "configVersion",
    "configVersionInc",
};

const std::set<std::string> kPassiveKeys{
    "ipGroup",
    "ipv6Group",
    "lanSetting",
    "lldp",
    "logSetting",
    "macFilterGlobal",
    "schedulerGlobal",
    "schedulerAssoc",
    "snmp",
    "ssh",
    "wirelessAdv_2G",
    "wirelessAdv_5G",
    "wirelessAdv_5G2",
    "wirelessAdv_6G",
};

const std::set<std::string> kKnownKeys{
    "clientConfig",
    "clientOperation",
    "clientOperation_cmd",
    "clientRateConfig",
    "led",
    "managementVlan",
    "portalConfigList",
    "portalFreePolicyConfig",
    "ssid_2G",
    "ssid_5G",
    "ssid_5G2",
    "ssid_6G",
    "wifiControlLed",
    "wirelessBasic_2G",
    "wirelessBasic_5G",
    "wirelessBasic_5G2",
    "wirelessBasic_6G",
};

ConfigParseResult fail(std::string message) {
    ConfigParseResult result;
    result.error = std::move(message);
    return result;
}

bool is_object(json_object* value) noexcept {
    return value != nullptr && json_object_is_type(value, json_type_object);
}

bool is_array(json_object* value) noexcept {
    return value != nullptr && json_object_is_type(value, json_type_array);
}

std::optional<std::string> optional_string(json_object* value) {
    if (value == nullptr || json_object_is_type(value, json_type_null)) {
        return std::nullopt;
    }
    const char* text = json_object_get_string(value);
    if (text == nullptr) {
        return std::nullopt;
    }
    return std::string(text);
}

std::optional<std::int64_t> optional_int(json_object* value) {
    if (value == nullptr || json_object_is_type(value, json_type_null)) {
        return std::nullopt;
    }
    if (json_object_is_type(value, json_type_string)) {
        auto text = optional_string(value);
        if (!text.has_value() || text->empty()) {
            return std::nullopt;
        }
        char* end = nullptr;
        errno = 0;
        const long long parsed = std::strtoll(text->c_str(), &end, 10);
        if (errno != 0 || end == text->c_str() || (end != nullptr && *end != '\0')) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(parsed);
    }
    if (!json_object_is_type(value, json_type_int) &&
        !json_object_is_type(value, json_type_boolean) &&
        !json_object_is_type(value, json_type_double)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(json_object_get_int64(value));
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::optional<bool> optional_bool(json_object* value) {
    if (value == nullptr || json_object_is_type(value, json_type_null)) {
        return std::nullopt;
    }
    if (json_object_is_type(value, json_type_boolean)) {
        return json_object_get_boolean(value) != 0;
    }
    if (json_object_is_type(value, json_type_int)) {
        return json_object_get_int64(value) != 0;
    }
    if (json_object_is_type(value, json_type_string)) {
        auto text = optional_string(value);
        if (!text.has_value()) {
            return std::nullopt;
        }
        const std::string normalized = lowercase(*text);
        return normalized == "1" ||
               normalized == "true" ||
               normalized == "yes" ||
               normalized == "on" ||
               normalized == "enable" ||
               normalized == "enabled";
    }
    return json_object_get_boolean(value) != 0;
}

bool enabled_string(json_object* value) {
    return optional_bool(value).value_or(false);
}

std::optional<std::uint16_t> optional_vlan_id(json_object* value, std::string* error) {
    auto parsed = optional_int(value);
    if (!parsed.has_value() || *parsed == 0) {
        return std::nullopt;
    }
    if (*parsed < 1 || *parsed > 4094) {
        if (error != nullptr) {
            *error = "VLAN ID must be between 1 and 4094";
        }
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(*parsed);
}

bool validate_ssid_name(const std::string& name, std::string* error) {
    if (name.find('\0') != std::string::npos) {
        if (error != nullptr) {
            *error = "SSID cannot contain NUL bytes";
        }
        return false;
    }
    if (name.size() > 32) {
        if (error != nullptr) {
            *error = "SSID must fit in 32 UTF-8 bytes";
        }
        return false;
    }
    return true;
}

bool active_mapping(json_object* value) {
    if (!is_object(value)) {
        return false;
    }
    static constexpr const char* kFlags[] = {
        "enable",
        "enabled",
        "radiusEnable",
        "authEnable",
        "accountingEnable",
    };
    for (const char* key : kFlags) {
        json_object* raw = protocol::object_member(value, key);
        if (raw != nullptr && !optional_bool(raw).value_or(false)) {
            return false;
        }
    }
    return true;
}

std::vector<json_object*> iter_items(json_object* raw, std::string_view label, std::string* error) {
    std::vector<json_object*> items;
    if (raw == nullptr || json_object_is_type(raw, json_type_null)) {
        return items;
    }
    if (is_object(raw)) {
        items.push_back(raw);
        return items;
    }
    if (is_array(raw)) {
        const auto length = json_object_array_length(raw);
        items.reserve(length);
        for (std::size_t index = 0; index < length; ++index) {
            items.push_back(json_object_array_get_idx(raw, index));
        }
        return items;
    }
    if (error != nullptr) {
        *error = std::string(label) + " must be an object or array";
    }
    return items;
}

bool require_object(json_object* raw, std::string_view label, std::string* error) {
    if (is_object(raw)) {
        return true;
    }
    if (error != nullptr) {
        *error = std::string(label) + " must be a JSON object";
    }
    return false;
}

RadioConfig parse_radio_config(RadioBand band, json_object* raw) {
    RadioConfig radio;
    radio.band = band;
    radio.radio_id = optional_int(protocol::object_member(raw, "radioId"));
    radio.enabled = optional_bool(protocol::object_member(raw, "radioEnable"));
    radio.channel_width = optional_int(protocol::object_member(raw, "chanWidth"));
    radio.channel = optional_int(protocol::object_member(raw, "channel"));
    radio.tx_power = optional_int(protocol::object_member(raw, "txPower"));
    radio.channel_limit = optional_bool(protocol::object_member(raw, "channelLimit"));
    radio.wireless_mode = optional_int(protocol::object_member(raw, "wirelessMode"));
    return radio;
}

DhcpOption82Intent parse_dhcp_option82(json_object* raw) {
    DhcpOption82Intent option;
    option.enabled = optional_bool(protocol::object_member(raw, "option82En")).value_or(false);
    option.format = optional_int(protocol::object_member(raw, "option82Format"));
    option.delimiter = optional_string(protocol::object_member(raw, "delimiter")).value_or("");
    option.site_name = optional_string(protocol::object_member(raw, "siteName")).value_or("");

    for (const char* key : {"circuitId", "remoteId"}) {
        json_object* array = protocol::object_member(raw, key);
        if (!is_array(array)) {
            continue;
        }
        auto& target = std::string(key) == "circuitId" ? option.circuit_id : option.remote_id;
        const auto length = json_object_array_length(array);
        for (std::size_t index = 0; index < length; ++index) {
            auto value = optional_int(json_object_array_get_idx(array, index));
            if (value.has_value()) {
                target.push_back(*value);
            }
        }
    }
    return option;
}

WirelessNetwork parse_ssid_item(RadioBand band, std::optional<std::int64_t> radio_id, json_object* raw, std::string* error) {
    WirelessNetwork wlan;
    wlan.band = band;
    wlan.radio_id = radio_id;
    wlan.ssid_id = optional_int(protocol::object_member(raw, "id"));
    wlan.index = optional_int(protocol::object_member(raw, "index"));
    wlan.operation = optional_int(protocol::object_member(raw, "operation"));
    wlan.name = optional_string(protocol::object_member(raw, "ssidName")).value_or("");
    if (!validate_ssid_name(wlan.name, error)) {
        return wlan;
    }
    wlan.broadcast = optional_bool(protocol::object_member(raw, "ssidBcast"));
    wlan.client_isolation = optional_bool(protocol::object_member(raw, "ssidIsolation"));

    wlan.vlan.vlan_id = optional_vlan_id(protocol::object_member(raw, "vlanId"), error);
    if (error != nullptr && !error->empty()) {
        return wlan;
    }
    json_object* pools = protocol::object_member(raw, "vlanPoolIds");
    if (is_array(pools)) {
        const auto length = json_object_array_length(pools);
        for (std::size_t index = 0; index < length; ++index) {
            wlan.vlan.vlan_pool_ids.push_back(
                optional_string(json_object_array_get_idx(pools, index)).value_or("")
            );
        }
    }
    wlan.vlan.dynamic_vlan_mode = optional_int(protocol::object_member(raw, "dyVlanMode"));
    json_object* dhcp = protocol::object_member(raw, "dhcpOp82");
    if (dhcp != nullptr) {
        if (!require_object(dhcp, "DHCP option 82 config", error)) {
            return wlan;
        }
        wlan.vlan.dhcp_option82 = parse_dhcp_option82(dhcp);
    }

    wlan.security.security_mode = optional_int(protocol::object_member(raw, "securityMode"));
    wlan.security.auth_type = optional_int(protocol::object_member(raw, "authType"));
    wlan.security.wpa_version = optional_int(protocol::object_member(raw, "wpaVer"));
    wlan.security.wpa_cipher = optional_int(protocol::object_member(raw, "wpaCipher"));
    wlan.security.psk_version = optional_int(protocol::object_member(raw, "pskVer"));
    wlan.security.psk_cipher = optional_int(protocol::object_member(raw, "pskCipher"));
    wlan.security.psk_key = optional_string(protocol::object_member(raw, "pskKey")).value_or("");
    wlan.security.psk_configured = protocol::object_member(raw, "pskKey") != nullptr;
    wlan.security.radius_profile_id = optional_string(protocol::object_member(raw, "wpaRadiusProfileId")).value_or("");
    wlan.security.radius_auth = active_mapping(protocol::object_member(raw, "radiusAuth"));
    wlan.security.radius_accounting = active_mapping(protocol::object_member(raw, "radiusAccounting"));
    wlan.security.radius_mac_auth = active_mapping(protocol::object_member(raw, "macAuth"));
    wlan.security.pmf_mode = optional_int(protocol::object_member(raw, "pmfMode"));
    json_object* fast = protocol::object_member(raw, "fastTransition");
    if (is_object(fast)) {
        wlan.security.fast_roaming = optional_bool(protocol::object_member(fast, "enable11r"));
    }

    wlan.portal.enabled = optional_bool(protocol::object_member(raw, "portal")).value_or(false);
    wlan.portal.https_redirect = optional_bool(protocol::object_member(raw, "httpsRedirectEnable"));
    wlan.portal.hotspot_v2_present = is_object(protocol::object_member(raw, "hotspotV2"));
    return wlan;
}

void parse_ssid_config(AccessPointConfigUpdate* update, RadioBand band, json_object* raw, std::string* error) {
    if (!require_object(raw, "SSID config", error)) {
        return;
    }
    auto radio_id = optional_int(protocol::object_member(raw, "radioId"));
    json_object* ssids = protocol::object_member(raw, "ssid");
    for (json_object* item : iter_items(ssids, "SSID list", error)) {
        if (error != nullptr && !error->empty()) {
            return;
        }
        if (!require_object(item, "SSID item", error)) {
            return;
        }
        auto wlan = parse_ssid_item(band, radio_id, item, error);
        if (error != nullptr && !error->empty()) {
            return;
        }
        update->wlans.push_back(std::move(wlan));
    }
}

std::optional<ManagementVlan> parse_management_vlan(json_object* raw, std::string* error) {
    if (!require_object(raw, "management VLAN config", error)) {
        return std::nullopt;
    }
    ManagementVlan vlan;
    vlan.enabled = enabled_string(protocol::object_member(raw, "managementVlanEnable"));
    vlan.vlan_id = optional_vlan_id(protocol::object_member(raw, "managementVlanId"), error);
    return vlan;
}

PortalFreePolicy parse_portal_free_policy(json_object* raw, std::string* error) {
    PortalFreePolicy policy;
    if (!require_object(raw, "portal free policy config", error)) {
        return policy;
    }
    policy.layer2_rule_count = iter_items(protocol::object_member(raw, "portalFreePolicy"), "portal free policy item", error).size();
    if (error != nullptr && !error->empty()) {
        return policy;
    }
    policy.url_rule_count = iter_items(protocol::object_member(raw, "urlPortalFreePolicy"), "portal URL free policy item", error).size();
    return policy;
}

PortalConfiguration parse_portal_config(json_object* raw) {
    PortalConfiguration portal;
    portal.auth_type = optional_int(protocol::object_member(raw, "authType"));
    portal.auth_timeout = optional_int(protocol::object_member(raw, "authTimeout"));
    portal.portal_day = optional_int(protocol::object_member(raw, "portalDay"));
    portal.portal_hour = optional_int(protocol::object_member(raw, "portalHour"));
    portal.portal_min = optional_int(protocol::object_member(raw, "portalMin"));
    portal.https_redirect_enable = optional_bool(protocol::object_member(raw, "httpsRedirectEnable"));
    portal.redirect = optional_bool(protocol::object_member(raw, "redirect"));
    portal.redirect_url = optional_string(protocol::object_member(raw, "redirectUrl")).value_or("");
    portal.auth_server_type = optional_int(protocol::object_member(raw, "authServerType"));
    portal.ext_auth_server = optional_string(protocol::object_member(raw, "extAuthServer")).value_or("");
    portal.external_portal_server = optional_string(protocol::object_member(raw, "externalPortalServer")).value_or("");
    portal.site_id = optional_string(protocol::object_member(raw, "siteId")).value_or("");
    portal.site_name = optional_string(protocol::object_member(raw, "siteName")).value_or(
        optional_string(protocol::object_member(raw, "site")).value_or("")
    );
    portal.portal_title = optional_string(protocol::object_member(raw, "portalTitle")).value_or("");
    portal.portal_accept = optional_bool(protocol::object_member(raw, "portalAccept"));
    json_object* ssid_list = protocol::object_member(raw, "ssidList");
    if (is_array(ssid_list)) {
        const auto length = json_object_array_length(ssid_list);
        for (std::size_t index = 0; index < length; ++index) {
            portal.ssid_list.push_back(optional_string(json_object_array_get_idx(ssid_list, index)).value_or(""));
        }
    }
    return portal;
}

LedConfig parse_led(json_object* raw) {
    LedConfig led;
    led.enabled = optional_bool(protocol::object_member(raw, "enable"));
    led.locate = optional_bool(protocol::object_member(raw, "locate"));
    return led;
}

WifiControlLedConfig parse_wifi_control_led(json_object* raw) {
    WifiControlLedConfig led;
    led.enabled = optional_bool(protocol::object_member(raw, "enable"));
    led.is_pressed = optional_bool(protocol::object_member(raw, "isPressed"));
    return led;
}

std::optional<domain::MacAddress> required_mac(json_object* raw, std::string_view label, std::string* error) {
    auto text = optional_string(raw);
    if (!text.has_value() || text->empty()) {
        if (error != nullptr) {
            *error = std::string(label) + " is required";
        }
        return std::nullopt;
    }
    auto parsed = domain::MacAddress::parse(*text);
    if (!parsed.has_value()) {
        if (error != nullptr) {
            *error = std::string(label) + " is invalid";
        }
        return std::nullopt;
    }
    return parsed;
}

void parse_client_config(AccessPointConfigUpdate* update, json_object* raw, std::string* error) {
    for (json_object* item : iter_items(raw, "clientConfig", error)) {
        if (error != nullptr && !error->empty()) {
            return;
        }
        if (!require_object(item, "clientConfig item", error)) {
            return;
        }
        auto mac = required_mac(protocol::object_member(item, "clientMac"), "clientConfig.clientMac", error);
        if (!mac.has_value()) {
            return;
        }
        ClientAuthConfig config;
        config.client_mac = *mac;
        config.unauthenticated = optional_bool(protocol::object_member(item, "unauth"));
        update->client_configs.push_back(config);
    }
}

void parse_client_operations(AccessPointConfigUpdate* update, json_object* raw, const char* source_key, std::string* error) {
    for (json_object* item : iter_items(raw, source_key, error)) {
        if (error != nullptr && !error->empty()) {
            return;
        }
        if (!require_object(item, "clientOperation item", error)) {
            return;
        }
        auto mac = required_mac(protocol::object_member(item, "clientMac"), std::string(source_key) + ".clientMac", error);
        if (!mac.has_value()) {
            return;
        }
        ClientControlOperation operation;
        operation.client_mac = *mac;
        operation.operation = optional_int(protocol::object_member(item, "operation"));
        operation.ssid = optional_string(protocol::object_member(item, "ssid")).value_or("");
        operation.radio_id = optional_int(protocol::object_member(item, "radioId"));
        operation.vid = optional_int(protocol::object_member(item, "vid"));
        operation.port = optional_int(protocol::object_member(item, "port"));
        operation.wireless = optional_bool(protocol::object_member(item, "wireless"));
        operation.source_key = source_key;
        update->client_operations.push_back(std::move(operation));
    }
}

void parse_client_rate_config(AccessPointConfigUpdate* update, json_object* raw, std::string* error) {
    if (!require_object(raw, "clientRateConfig", error)) {
        return;
    }
    ClientRateConfig config;
    config.action = optional_int(protocol::object_member(raw, "action"));
    for (json_object* item : iter_items(protocol::object_member(raw, "clientRateLimit"), "clientRateLimit", error)) {
        if (error != nullptr && !error->empty()) {
            return;
        }
        if (!require_object(item, "clientRateLimit item", error)) {
            return;
        }
        auto mac = required_mac(protocol::object_member(item, "mac"), "clientRateLimit.mac", error);
        if (!mac.has_value()) {
            return;
        }
        ClientRateLimit limit;
        limit.mac = *mac;
        limit.down = optional_int(protocol::object_member(item, "down"));
        limit.up = optional_int(protocol::object_member(item, "up"));
        config.limits.push_back(limit);
    }
    update->client_rate_config = std::move(config);
}

ConfigParseResult parse_body_object(json_object* body) {
    if (!is_object(body)) {
        return fail("SET_REQUEST body must be a JSON object");
    }

    ConfigParseResult result;
    result.ok = true;
    auto& update = result.update;
    update.sequence_id = optional_int(protocol::object_member(body, "sequenceId"));
    update.config_version = optional_int(protocol::object_member(body, "configVersion"));
    update.config_version_inc = optional_int(protocol::object_member(body, "configVersionInc"));

    std::string error;
    struct RadioKey {
        const char* key;
        RadioBand band;
    };
    for (const RadioKey radio_key : {
             RadioKey{"wirelessBasic_2G", RadioBand::TwoG},
             RadioKey{"wirelessBasic_5G", RadioBand::FiveG},
             RadioKey{"wirelessBasic_5G2", RadioBand::FiveG2},
             RadioKey{"wirelessBasic_6G", RadioBand::SixG},
         }) {
        json_object* raw = protocol::object_member(body, radio_key.key);
        if (raw == nullptr) {
            continue;
        }
        if (!require_object(raw, "radio config", &error)) {
            return fail(error);
        }
        update.radios.push_back(parse_radio_config(radio_key.band, raw));
    }

    struct SsidKey {
        const char* key;
        RadioBand band;
    };
    for (const SsidKey ssid_key : {
             SsidKey{"ssid_2G", RadioBand::TwoG},
             SsidKey{"ssid_5G", RadioBand::FiveG},
             SsidKey{"ssid_5G2", RadioBand::FiveG2},
             SsidKey{"ssid_6G", RadioBand::SixG},
         }) {
        json_object* raw = protocol::object_member(body, ssid_key.key);
        if (raw == nullptr) {
            continue;
        }
        parse_ssid_config(&update, ssid_key.band, raw, &error);
        if (!error.empty()) {
            return fail(error);
        }
    }

    if (json_object* raw = protocol::object_member(body, "managementVlan")) {
        update.management_vlan = parse_management_vlan(raw, &error);
        if (!error.empty()) {
            return fail(error);
        }
    }
    if (json_object* raw = protocol::object_member(body, "portalFreePolicyConfig")) {
        update.portal_free_policy = parse_portal_free_policy(raw, &error);
        if (!error.empty()) {
            return fail(error);
        }
    }
    if (json_object* raw = protocol::object_member(body, "portalConfigList")) {
        for (json_object* item : iter_items(raw, "portalConfigList", &error)) {
            if (!error.empty()) {
                return fail(error);
            }
            if (!require_object(item, "portalConfigList item", &error)) {
                return fail(error);
            }
            update.portal_configs.push_back(parse_portal_config(item));
        }
    }
    if (json_object* raw = protocol::object_member(body, "led")) {
        if (!require_object(raw, "LED config", &error)) {
            return fail(error);
        }
        update.led = parse_led(raw);
    }
    if (json_object* raw = protocol::object_member(body, "wifiControlLed")) {
        if (!require_object(raw, "WiFi control LED config", &error)) {
            return fail(error);
        }
        update.wifi_control_led = parse_wifi_control_led(raw);
    }
    if (json_object* raw = protocol::object_member(body, "clientConfig")) {
        parse_client_config(&update, raw, &error);
        if (!error.empty()) {
            return fail(error);
        }
    }
    if (json_object* raw = protocol::object_member(body, "clientOperation")) {
        parse_client_operations(&update, raw, "clientOperation", &error);
        if (!error.empty()) {
            return fail(error);
        }
    }
    if (json_object* raw = protocol::object_member(body, "clientOperation_cmd")) {
        parse_client_operations(&update, raw, "clientOperation_cmd", &error);
        if (!error.empty()) {
            return fail(error);
        }
    }
    if (json_object* raw = protocol::object_member(body, "clientRateConfig")) {
        parse_client_rate_config(&update, raw, &error);
        if (!error.empty()) {
            return fail(error);
        }
    }

    json_object_object_foreach(body, key, value) {
        (void)value;
        const std::string key_name(key);
        if (kPassiveKeys.count(key_name) != 0U) {
            update.passive_keys.push_back(key_name);
        }
        if (kCommonKeys.count(key_name) == 0U &&
            kKnownKeys.count(key_name) == 0U &&
            kPassiveKeys.count(key_name) == 0U) {
            update.unhandled_keys.push_back(key_name);
        }
    }
    std::sort(update.passive_keys.begin(), update.passive_keys.end());
    std::sort(update.unhandled_keys.begin(), update.unhandled_keys.end());
    return result;
}

template <typename T>
void append_optional(std::string* out, const char* label, std::optional<T> value) {
    *out += label;
    *out += "=";
    if (value.has_value()) {
        *out += std::to_string(*value);
    } else {
        *out += "None";
    }
}

std::string optional_bool_word(std::optional<bool> value) {
    if (!value.has_value()) {
        return "unset";
    }
    return *value ? "True" : "False";
}

} // namespace

const char* to_wire_string(RadioBand band) noexcept {
    switch (band) {
    case RadioBand::TwoG:
        return "2g";
    case RadioBand::FiveG:
        return "5g";
    case RadioBand::FiveG2:
        return "5g2";
    case RadioBand::SixG:
        return "6g";
    }
    return "unknown";
}

ConfigParseResult parse_config_body_json(std::string_view body_json) noexcept {
    auto document = protocol::JsonDocument::parse(body_json);
    if (!document.valid()) {
        return fail("SET_REQUEST body must be a JSON object");
    }
    return parse_body_object(document.get());
}

ConfigParseResult parse_set_request_json(std::string_view message_json) noexcept {
    auto document = protocol::JsonDocument::parse(message_json);
    if (!document.valid()) {
        return fail("SET_REQUEST must be a JSON object");
    }
    json_object* body = protocol::ecsp_body(document.get());
    if (!is_object(body)) {
        return fail("SET_REQUEST body must be a JSON object");
    }
    return parse_body_object(body);
}

std::string describe_config_update(const AccessPointConfigUpdate& update) {
    std::string out;
    append_optional(&out, "sequenceId", update.sequence_id);
    out += " ";
    append_optional(&out, "configVersion", update.config_version);
    out += " ";
    append_optional(&out, "configVersionInc", update.config_version_inc);

    if (!update.radios.empty()) {
        out += " radios=";
        out += std::to_string(update.radios.size());
        out += "[";
        for (std::size_t index = 0; index < update.radios.size(); ++index) {
            if (index != 0) {
                out += ",";
            }
            out += to_wire_string(update.radios[index].band);
        }
        out += "]";
    }
    if (!update.wlans.empty()) {
        out += " wlans=";
        out += std::to_string(update.wlans.size());
        out += "[";
        for (std::size_t index = 0; index < update.wlans.size(); ++index) {
            if (index != 0) {
                out += ",";
            }
            out += to_wire_string(update.wlans[index].band);
        }
        out += "]";
    }
    if (update.management_vlan.has_value()) {
        out += " managementVlan=";
        out += update.management_vlan->enabled ? "on:" : "off:";
        out += update.management_vlan->vlan_id.has_value() ? std::to_string(*update.management_vlan->vlan_id) : "None";
    }
    if (update.led.has_value()) {
        out += " led=enable:";
        out += optional_bool_word(update.led->enabled);
        out += ",locate:";
        out += optional_bool_word(update.led->locate);
    }
    if (update.wifi_control_led.has_value()) {
        out += " wifiControlLed=present";
    }
    if (update.portal_free_policy.has_value()) {
        out += " portalFreePolicy=l2:";
        out += std::to_string(update.portal_free_policy->layer2_rule_count);
        out += ",url:";
        out += std::to_string(update.portal_free_policy->url_rule_count);
    }
    if (!update.portal_configs.empty()) {
        out += " portalConfigList=";
        out += std::to_string(update.portal_configs.size());
    }
    if (!update.client_configs.empty()) {
        out += " clientConfig=";
        out += std::to_string(update.client_configs.size());
    }
    if (!update.client_operations.empty()) {
        out += " clientOperation=";
        out += std::to_string(update.client_operations.size());
        out += "[";
        for (std::size_t index = 0; index < update.client_operations.size(); ++index) {
            if (index != 0) {
                out += ",";
            }
            out += update.client_operations[index].operation.has_value()
                ? std::to_string(*update.client_operations[index].operation)
                : "unknown";
        }
        out += "]";
    }
    if (update.client_rate_config.has_value()) {
        out += " clientRateConfig=action:";
        out += update.client_rate_config->action.has_value() ? std::to_string(*update.client_rate_config->action) : "None";
        out += ",limits:";
        out += std::to_string(update.client_rate_config->limits.size());
    }
    if (!update.passive_keys.empty()) {
        out += " passive=";
        for (std::size_t index = 0; index < update.passive_keys.size(); ++index) {
            if (index != 0) {
                out += ",";
            }
            out += update.passive_keys[index];
        }
    }
    if (!update.ack_only_keys.empty()) {
        out += " ackOnly=";
        for (std::size_t index = 0; index < update.ack_only_keys.size(); ++index) {
            if (index != 0) {
                out += ",";
            }
            out += update.ack_only_keys[index];
        }
    }
    if (!update.unhandled_keys.empty()) {
        out += " unhandled=";
        for (std::size_t index = 0; index < update.unhandled_keys.size(); ++index) {
            if (index != 0) {
                out += ",";
            }
            out += update.unhandled_keys[index];
        }
    }
    return out;
}

bool is_actionable_config(const AccessPointConfigUpdate& update) noexcept {
    return !update.radios.empty() ||
           !update.wlans.empty() ||
           update.management_vlan.has_value() ||
           update.portal_free_policy.has_value() ||
           !update.portal_configs.empty() ||
           update.led.has_value() ||
           update.wifi_control_led.has_value() ||
           !update.client_configs.empty() ||
           !update.client_operations.empty() ||
           update.client_rate_config.has_value();
}

bool is_supported_config_update(const AccessPointConfigUpdate& update) noexcept {
    return update.unhandled_keys.empty();
}

} // namespace openomada::application
