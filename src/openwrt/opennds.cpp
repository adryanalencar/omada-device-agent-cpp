#include "openomada/openwrt/opennds.hpp"

#include "openomada/protocol/ecsp_message.hpp"
#include "openomada/protocol/json.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <json-c/json.h>
#include <set>

namespace openomada::openwrt {
namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    return value;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

void append_unique(std::vector<std::string>& values, std::string value) {
    value = trim(std::move(value));
    if (value.empty()) {
        return;
    }
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}

std::optional<std::uint8_t> ipv4_octet(std::string_view raw) noexcept {
    if (raw.empty() || raw.size() > 3) {
        return std::nullopt;
    }
    unsigned value = 0;
    for (char ch : raw) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return std::nullopt;
        }
        value = value * 10U + static_cast<unsigned>(ch - '0');
    }
    if (value > 255U) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(value);
}

bool valid_ipv4_address(std::string_view raw) noexcept {
    std::size_t offset = 0;
    for (int index = 0; index < 4; ++index) {
        const std::size_t dot = raw.find('.', offset);
        const std::size_t end = dot == std::string_view::npos ? raw.size() : dot;
        if (!ipv4_octet(raw.substr(offset, end - offset)).has_value()) {
            return false;
        }
        if (index < 3) {
            if (dot == std::string_view::npos) {
                return false;
            }
            offset = dot + 1;
        } else if (dot != std::string_view::npos) {
            return false;
        }
    }
    return true;
}

bool valid_mask(std::int64_t mask) noexcept {
    return mask >= 0 && mask <= 32;
}

std::optional<std::uint16_t> optional_port_from_host(std::string_view host_port) noexcept {
    const std::size_t colon = host_port.rfind(':');
    if (colon == std::string_view::npos || colon + 1 >= host_port.size()) {
        return std::nullopt;
    }
    std::uint32_t port = 0;
    for (char ch : host_port.substr(colon + 1)) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return std::nullopt;
        }
        port = port * 10U + static_cast<std::uint32_t>(ch - '0');
        if (port > 65535U) {
            return std::nullopt;
        }
    }
    return static_cast<std::uint16_t>(port);
}

std::string host_from_urlish(std::string raw) {
    raw = trim(std::move(raw));
    if (raw.empty()) {
        return {};
    }
    const std::size_t scheme = raw.find("://");
    if (scheme != std::string::npos) {
        raw.erase(0, scheme + 3);
    } else if (raw.rfind("//", 0) == 0) {
        raw.erase(0, 2);
    }
    if (const std::size_t slash = raw.find_first_of("/?#"); slash != std::string::npos) {
        raw.resize(slash);
    }
    if (const std::size_t at = raw.rfind('@'); at != std::string::npos) {
        raw.erase(0, at + 1);
    }
    if (!raw.empty() && raw.front() == '[') {
        return {};
    }
    if (const std::size_t colon = raw.rfind(':'); colon != std::string::npos) {
        raw.resize(colon);
    }
    while (!raw.empty() && raw.back() == '.') {
        raw.pop_back();
    }
    raw = lowercase(trim(std::move(raw)));
    if (raw.empty() || valid_ipv4_address(raw)) {
        return {};
    }
    return raw;
}

std::string normal_portal_url(std::string raw, bool require_portal_path = false) {
    raw = trim(std::move(raw));
    if (raw.empty()) {
        return {};
    }
    std::string normalized = raw.find("://") == std::string::npos ? "https://" + raw : raw;
    if (normalized.rfind("http://", 0) != 0 && normalized.rfind("https://", 0) != 0) {
        return {};
    }
    const std::size_t host_begin = normalized.find("://") + 3;
    const std::size_t path_begin = normalized.find_first_of("/?#", host_begin);
    const std::string host = normalized.substr(host_begin, path_begin == std::string::npos ? std::string::npos : path_begin - host_begin);
    if (host.empty()) {
        return {};
    }
    if (require_portal_path) {
        const std::string path = path_begin == std::string::npos ? "" : lowercase(normalized.substr(path_begin));
        if (path.find("portal") == std::string::npos) {
            return {};
        }
    }
    return normalized;
}

std::string network_from_rule(const application::PortalLayer2Rule& rule) {
    std::string raw;
    for (const auto& candidate : {rule.dst_ip, rule.value, rule.ip, rule.ip_address, rule.address, rule.dst}) {
        if (!trim(candidate).empty()) {
            raw = trim(candidate);
            break;
        }
    }
    if (raw.empty()) {
        return {};
    }
    const std::int64_t mask = rule.dst_mask.value_or(rule.mask.value_or(-1));
    if (mask >= 0) {
        if (!valid_mask(mask) || !valid_ipv4_address(raw)) {
            return {};
        }
        return raw + "/" + std::to_string(mask);
    }
    if (!valid_ipv4_address(raw)) {
        return {};
    }
    return raw;
}

std::string portal_redirect_url(
    const application::AccessPointConfigUpdate& update,
    const std::string& controller_host
) {
    for (const auto& portal : update.portal_configs) {
        for (const auto& raw : {portal.external_portal_server, portal.ext_auth_server}) {
            const std::string normalized = normal_portal_url(raw);
            if (!normalized.empty()) {
                return normalized;
            }
        }
    }
    if (update.portal_free_policy.has_value()) {
        for (const auto& rule : update.portal_free_policy->url_rules) {
            for (const auto& raw : {rule.url, rule.host, rule.value}) {
                const std::string normalized = normal_portal_url(raw, true);
                if (!normalized.empty()) {
                    return normalized;
                }
            }
        }
    }
    for (const auto& portal : update.portal_configs) {
        if (portal.redirect.has_value() && !*portal.redirect) {
            continue;
        }
        const std::string normalized = normal_portal_url(portal.redirect_url);
        if (!normalized.empty()) {
            return normalized;
        }
    }
    std::string host = trim(controller_host);
    if (host.empty()) {
        return {};
    }
    std::string scheme = "http";
    if (host.find("://") != std::string::npos) {
        scheme = host.rfind("https://", 0) == 0 ? "https" : "http";
        host = host.substr(host.find("://") + 3);
    }
    if (const std::size_t slash = host.find('/'); slash != std::string::npos) {
        host.resize(slash);
    }
    if (optional_port_from_host(host).has_value()) {
        return scheme + "://" + host + "/portal/entry";
    }
    const std::uint16_t port = scheme == "https" ? 8843 : 8088;
    return scheme + "://" + host + ":" + std::to_string(port) + "/portal/entry";
}

std::string landing_page_url(const std::vector<application::PortalConfiguration>& portals) {
    for (const auto& portal : portals) {
        if (portal.redirect.has_value() && !*portal.redirect) {
            continue;
        }
        const std::string normalized = normal_portal_url(portal.redirect_url);
        if (!normalized.empty()) {
            return normalized;
        }
    }
    return {};
}

std::string default_ssid_name(const std::vector<application::PortalConfiguration>& portals) {
    for (const auto& portal : portals) {
        for (const auto& ssid : portal.ssid_list) {
            const std::string normalized = trim(ssid);
            if (!normalized.empty()) {
                return normalized;
            }
        }
    }
    return {};
}

std::string portal_site_id(const std::vector<application::PortalConfiguration>& portals, const std::string& configured) {
    for (const auto& portal : portals) {
        const std::string normalized = trim(portal.site_id);
        if (!normalized.empty()) {
            return normalized;
        }
    }
    return trim(configured);
}

std::string portal_site_name(const std::vector<application::PortalConfiguration>& portals, const std::string& configured) {
    for (const auto& portal : portals) {
        const std::string normalized = trim(portal.site_name);
        if (!normalized.empty()) {
            return normalized;
        }
    }
    return trim(configured);
}

std::string shell_quote(std::string_view value) {
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
}

std::string html_escape(std::string_view value) {
    std::string out;
    for (char ch : value) {
        switch (ch) {
        case '&':
            out += "&amp;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

std::optional<std::int64_t> optional_int_any(json_object* value) noexcept {
    if (value == nullptr || json_object_is_type(value, json_type_null)) {
        return std::nullopt;
    }
    if (json_object_is_type(value, json_type_string)) {
        const char* text = json_object_get_string(value);
        if (text == nullptr || *text == '\0') {
            return std::nullopt;
        }
        char* end = nullptr;
        errno = 0;
        const long long parsed = std::strtoll(text, &end, 10);
        if (errno != 0 || end == text || (end != nullptr && *end != '\0')) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(parsed);
    }
    if (!json_object_is_type(value, json_type_int) &&
        !json_object_is_type(value, json_type_double) &&
        !json_object_is_type(value, json_type_boolean)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(json_object_get_int64(value));
}

std::optional<std::string> optional_string_any(json_object* value) {
    if (value == nullptr || json_object_is_type(value, json_type_null)) {
        return std::nullopt;
    }
    const char* text = json_object_get_string(value);
    if (text == nullptr || *text == '\0') {
        return std::nullopt;
    }
    return std::string(text);
}

application::ClientPortalState portal_state_from_opennds(std::string raw) {
    raw = lowercase(trim(std::move(raw)));
    if (raw == "authenticated" || raw == "trusted") {
        return application::ClientPortalState::Authenticated;
    }
    if (raw == "preauthenticated" || raw == "preauth") {
        return application::ClientPortalState::Unauthenticated;
    }
    if (raw == "blocked") {
        return application::ClientPortalState::Blocked;
    }
    return application::ClientPortalState::Unknown;
}

std::uint64_t uint_counter(json_object* object, const char* key) noexcept {
    auto parsed = optional_int_any(protocol::object_member(object, key));
    if (!parsed.has_value() || *parsed < 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(*parsed);
}

} // namespace

OpenNdsPortalPolicy opennds_portal_policy_from_free_policy(
    const std::optional<application::PortalFreePolicy>& policy
) {
    OpenNdsPortalPolicy out;
    if (!policy.has_value()) {
        return out;
    }
    for (const auto& rule : policy->url_rules) {
        for (const auto& raw : {rule.url, rule.host, rule.value}) {
            append_unique(out.walled_garden_fqdns, host_from_urlish(raw));
        }
    }
    for (const auto& rule : policy->layer2_rules) {
        const std::string network = network_from_rule(rule);
        if (!network.empty()) {
            append_unique(out.preauthenticated_user_rules, "allow all to " + network);
        }
    }
    return out;
}

OpenNdsPortalPolicy opennds_portal_policy_from_omada_config(
    const application::AccessPointConfigUpdate& update,
    const std::string& controller_host,
    const domain::MacAddress& device_mac,
    const std::string& configured_site_id,
    const std::string& configured_site_name
) {
    OpenNdsPortalPolicy base = opennds_portal_policy_from_free_policy(update.portal_free_policy);
    base.portal_redirect_url = portal_redirect_url(update, controller_host);
    base.landing_page_url = landing_page_url(update.portal_configs);
    base.default_ssid_name = default_ssid_name(update.portal_configs);
    base.ap_mac = device_mac.omada();
    base.site_id = portal_site_id(update.portal_configs, configured_site_id);
    base.site_name = portal_site_name(update.portal_configs, configured_site_name);
    return base;
}

OpenNdsApplyPlan build_opennds_apply_plan(const OpenNdsPortalPolicy& policy) {
    OpenNdsApplyPlan plan;
    for (const auto& command : {
             std::vector<std::string>{"uci", "-q", "delete", "opennds.@opennds[0].walledgarden_fqdn_list"},
             std::vector<std::string>{"uci", "-q", "delete", "opennds.@opennds[0].walledgarden_port_list"},
             std::vector<std::string>{"uci", "-q", "delete", "opennds.@opennds[0].preauthenticated_users"},
             std::vector<std::string>{"uci", "-q", "delete", "opennds.@opennds[0].themespec_path"},
         }) {
        plan.commands.push_back(command);
    }
    for (const auto& fqdn : policy.walled_garden_fqdns) {
        plan.commands.push_back({"uci", "add_list", "opennds.@opennds[0].walledgarden_fqdn_list=" + fqdn});
    }
    if (!policy.walled_garden_fqdns.empty()) {
        std::string ports;
        for (std::size_t index = 0; index < policy.walled_garden_ports.size(); ++index) {
            if (index != 0) {
                ports.push_back(' ');
            }
            ports += std::to_string(policy.walled_garden_ports[index]);
        }
        plan.commands.push_back({"uci", "add_list", "opennds.@opennds[0].walledgarden_port_list=" + ports});
    }
    for (const auto& rule : policy.preauthenticated_user_rules) {
        plan.commands.push_back({"uci", "add_list", "opennds.@opennds[0].preauthenticated_users=" + rule});
    }
    if (!policy.portal_redirect_url.empty()) {
        plan.themespec = build_openomada_redirect_themespec(policy);
        plan.commands.push_back({"write-file", kOpenOmadaThemeSpecPath});
        plan.commands.push_back({"chmod", "0644", kOpenOmadaThemeSpecPath});
        plan.commands.push_back({"uci", "set", "opennds.@opennds[0].login_option_enabled=3"});
        plan.commands.push_back({"uci", "set", std::string("opennds.@opennds[0].themespec_path=") + kOpenOmadaThemeSpecPath});
    } else {
        plan.commands.push_back({"uci", "set", "opennds.@opennds[0].login_option_enabled=1"});
    }
    plan.commands.push_back({"uci", "set", "opennds.@opennds[0].gatewayfqdn=disable"});
    plan.commands.push_back({"uci", "set", "opennds.@opennds[0].allow_preemptive_authentication=0"});
    plan.commands.push_back({"uci", "commit", "opennds"});
    plan.commands.push_back({"/etc/init.d/opennds", "restart"});
    plan.ok = true;
    plan.changed = true;
    return plan;
}

std::string build_openomada_redirect_themespec(const OpenNdsPortalPolicy& policy) {
    if (policy.portal_redirect_url.empty()) {
        return {};
    }
    std::string script;
    script += "#!/bin/sh\n";
    script += "title=\"openomada-controller-redirect\"\n";
    script += "openomada_portal_url=" + shell_quote(policy.portal_redirect_url) + "\n";
    script += "openomada_landing_page_url=" + shell_quote(policy.landing_page_url) + "\n";
    script += "openomada_default_ssid_name=" + shell_quote(policy.default_ssid_name) + "\n";
    script += "openomada_ap_mac=" + shell_quote(policy.ap_mac) + "\n";
    script += "openomada_site_id=" + shell_quote(policy.site_id) + "\n";
    script += "openomada_site_name=" + shell_quote(policy.site_name) + "\n\n";
    script += "_openomada_format_mac() { printf \"%s\" \"$1\" | tr 'a-f:' 'A-F-'; }\n";
    script += "_openomada_hexdump() { command -v hexdump >/dev/null 2>&1 && hexdump -v -e '1/1 \"%02x\"'; }\n";
    script += "_openomada_urlencode() {\n";
    script += "    hex=$(printf \"%s\" \"$1\" | _openomada_hexdump 2>/dev/null)\n";
    script += "    encoded=\"\"\n";
    script += "    while [ -n \"$hex\" ]; do\n";
    script += "        byte=${hex%\"${hex#??}\"}; hex=${hex#??}\n";
    script += "        case \"$byte\" in\n";
    script += "            20) encoded=\"${encoded}+\" ;;\n";
    script += "            2d|2e|5f|7e|30|31|32|33|34|35|36|37|38|39|41|42|43|44|45|46|47|48|49|4a|4b|4c|4d|4e|4f|50|51|52|53|54|55|56|57|58|59|5a|61|62|63|64|65|66|67|68|69|6a|6b|6c|6d|6e|6f|70|71|72|73|74|75|76|77|78|79|7a) encoded=\"${encoded}$(printf \"\\\\x$byte\")\" ;;\n";
    script += "            *) encoded=\"${encoded}%$(printf \"%s\" \"$byte\" | tr 'a-f' 'A-F')\" ;;\n";
    script += "        esac\n";
    script += "    done\n";
    script += "    printf \"%s\" \"$encoded\"\n";
    script += "}\n";
    script += "_openomada_append_param() { key=$1; value=$2; case \"$openomada_target\" in *\\?|*\\&) sep=\"\" ;; *\\?*) sep=\"&\" ;; *) sep=\"?\" ;; esac; openomada_target=\"${openomada_target}${sep}${key}=$(_openomada_urlencode \"$value\")\"; }\n";
    script += "_openomada_now() { date +%s 2>/dev/null || printf \"0\"; }\n";
    script += "generate_splash_sequence() {\n";
    script += "    openomada_target=$openomada_portal_url\n";
    script += "    openomada_client_mac=$(_openomada_format_mac \"${clientmac:-}\")\n";
    script += "    openomada_client_ip=${clientip:-}\n";
    script += "    openomada_site_ref=${openomada_site_id:-$openomada_site_name}\n";
    script += "    openomada_resolved_ssid=${openomada_default_ssid_name:-${client_zone:-}}\n";
    script += "    openomada_redirect_url=$openomada_landing_page_url\n";
    script += "    [ -z \"$openomada_redirect_url\" ] && openomada_redirect_url=${originurl:-}\n";
    script += "    _openomada_append_param \"clientMac\" \"$openomada_client_mac\"\n";
    script += "    _openomada_append_param \"clientIp\" \"$openomada_client_ip\"\n";
    script += "    _openomada_append_param \"t\" \"$(_openomada_now)\"\n";
    script += "    _openomada_append_param \"site\" \"$openomada_site_ref\"\n";
    script += "    _openomada_append_param \"redirectUrl\" \"$openomada_redirect_url\"\n";
    script += "    _openomada_append_param \"apMac\" \"$(_openomada_format_mac \"$openomada_ap_mac\")\"\n";
    script += "    _openomada_append_param \"ssidName\" \"$openomada_resolved_ssid\"\n";
    script += "    _openomada_append_param \"radioId\" \"0\"\n";
    script += "    safe_target=$(printf \"%s\" \"$openomada_target\" | sed 's/&/\\&amp;/g; s/\"/\\&quot;/g; s/</\\&lt;/g; s/>/\\&gt;/g')\n";
    script += "    echo \"<meta http-equiv=\\\"refresh\\\" content=\\\"0; url=$safe_target\\\">\"\n";
    script += "    echo \"<p><a href=\\\"$safe_target\\\">Open Omada portal</a></p>\"\n";
    script += "}\n";
    script += "header() { echo \"<!DOCTYPE html><html><head><meta charset=\\\"utf-8\\\"><meta name=\\\"viewport\\\" content=\\\"width=device-width, initial-scale=1.0\\\"><title>Omada Portal</title></head><body>\"; }\n";
    script += "footer() { echo \"</body></html>\"; exit 0; }\n";
    script += "# Static portal base URL: " + html_escape(policy.portal_redirect_url) + "\n";
    return script;
}

OpenNdsClientParseResult opennds_clients_from_json(std::string_view payload_json) noexcept {
    OpenNdsClientParseResult result;
    protocol::JsonDocument document = protocol::JsonDocument::parse(payload_json);
    if (!document.valid()) {
        result.error = "openNDS payload must be a JSON object";
        return result;
    }
    auto* clients = protocol::object_member(document.get(), "clients");
    if (clients == nullptr || !json_object_is_type(clients, json_type_object)) {
        result.ok = true;
        return result;
    }
    json_object_object_foreach(clients, raw_mac, raw_client) {
        if (raw_client == nullptr || !json_object_is_type(raw_client, json_type_object)) {
            continue;
        }
        auto mac = domain::MacAddress::parse(optional_string_any(protocol::object_member(raw_client, "mac")).value_or(raw_mac));
        if (!mac.has_value()) {
            continue;
        }
        application::WirelessClientState client;
        client.mac = *mac;
        client.ipv4 = optional_string_any(protocol::object_member(raw_client, "ip")).value_or("");
        client.portal_state = portal_state_from_opennds(optional_string_any(protocol::object_member(raw_client, "state")).value_or(""));
        client.rx_bytes = uint_counter(raw_client, "download_this_session");
        client.tx_bytes = uint_counter(raw_client, "upload_this_session");
        result.clients.push_back(std::move(client));
    }
    std::sort(result.clients.begin(), result.clients.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.mac.normalized() < rhs.mac.normalized();
    });
    result.ok = true;
    return result;
}

} // namespace openomada::openwrt
