#include "openomada/application/daemon_config.hpp"

#include "openomada/platform/capabilities.hpp"

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <utility>

namespace openomada::application {
namespace {

using SectionValues = std::map<std::string, std::map<std::string, std::string>>;

std::string trim(std::string_view raw) {
    std::size_t begin = 0;
    while (begin < raw.size() && std::isspace(static_cast<unsigned char>(raw[begin])) != 0) {
        ++begin;
    }
    std::size_t end = raw.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(raw[end - 1])) != 0) {
        --end;
    }
    return std::string(raw.substr(begin, end - begin));
}

std::string lowercase(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::vector<std::string> split_uci_tokens(std::string_view line) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_single = false;
    bool in_double = false;
    bool escaped = false;
    for (char ch : line) {
        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_double) {
            escaped = true;
            continue;
        }
        if (ch == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }
        if (ch == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }
        if (!in_single && !in_double && ch == '#') {
            break;
        }
        if (!in_single && !in_double && std::isspace(static_cast<unsigned char>(ch)) != 0) {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

SectionValues parse_uci(std::string_view config_text) {
    SectionValues sections;
    std::string current_type;
    std::size_t offset = 0;
    while (offset <= config_text.size()) {
        const std::size_t newline = config_text.find('\n', offset);
        const std::size_t end = newline == std::string_view::npos ? config_text.size() : newline;
        const std::string line = trim(config_text.substr(offset, end - offset));
        const auto tokens = split_uci_tokens(line);
        if (!tokens.empty()) {
            if (tokens[0] == "config" && tokens.size() >= 2) {
                current_type = tokens[1];
                (void)sections[current_type];
            } else if (tokens[0] == "option" && tokens.size() >= 3 && !current_type.empty()) {
                sections[current_type][tokens[1]] = tokens[2];
            }
        }
        if (newline == std::string_view::npos) {
            break;
        }
        offset = newline + 1;
    }
    return sections;
}

std::optional<std::string> section_value(
    const SectionValues& sections,
    const std::string& section,
    const std::string& key
) {
    const auto section_it = sections.find(section);
    if (section_it == sections.end()) {
        return std::nullopt;
    }
    const auto value_it = section_it->second.find(key);
    if (value_it == section_it->second.end()) {
        return std::nullopt;
    }
    return value_it->second;
}

std::optional<std::string> env_value(
    const std::vector<EnvironmentVariable>& environment,
    std::string_view name
) {
    for (const auto& item : environment) {
        if (item.name == name) {
            return item.value;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> parse_uint32(std::string_view raw) noexcept {
    std::string value = trim(raw);
    if (value.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || (end != nullptr && *end != '\0') || parsed > 0xFFFFFFFFUL) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(parsed);
}

std::optional<std::uint16_t> parse_port(std::string_view raw) noexcept {
    const auto value = parse_uint32(raw);
    if (!value.has_value() || *value > 65535U) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(*value);
}

std::optional<bool> parse_bool(std::string_view raw) {
    const std::string value = lowercase(trim(raw));
    if (value == "1" || value == "true" || value == "yes" || value == "on" || value == "enabled" || value == "enable") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off" || value == "disabled" || value == "disable") {
        return false;
    }
    return std::nullopt;
}

void apply_string(
    const SectionValues& sections,
    const std::vector<EnvironmentVariable>& environment,
    const char* section,
    const char* key,
    const char* env,
    std::string* target
) {
    if (target == nullptr) {
        return;
    }
    if (auto value = section_value(sections, section, key)) {
        *target = *value;
    }
    if (auto value = env_value(environment, env)) {
        *target = *value;
    }
}

bool apply_bool(
    const SectionValues& sections,
    const std::vector<EnvironmentVariable>& environment,
    const char* section,
    const char* key,
    const char* env,
    bool* target,
    std::string* error
) {
    if (target == nullptr) {
        return true;
    }
    if (auto value = section_value(sections, section, key)) {
        auto parsed = parse_bool(*value);
        if (!parsed.has_value()) {
            if (error != nullptr) {
                *error = std::string(section) + "." + key + " must be boolean";
            }
            return false;
        }
        *target = *parsed;
    }
    if (auto value = env_value(environment, env)) {
        auto parsed = parse_bool(*value);
        if (!parsed.has_value()) {
            if (error != nullptr) {
                *error = std::string(env) + " must be boolean";
            }
            return false;
        }
        *target = *parsed;
    }
    return true;
}

bool apply_uint32(
    const SectionValues& sections,
    const std::vector<EnvironmentVariable>& environment,
    const char* section,
    const char* key,
    const char* env,
    std::uint32_t* target,
    std::string* error
) {
    if (target == nullptr) {
        return true;
    }
    if (auto value = section_value(sections, section, key)) {
        auto parsed = parse_uint32(*value);
        if (!parsed.has_value()) {
            if (error != nullptr) {
                *error = std::string(section) + "." + key + " must be an unsigned integer";
            }
            return false;
        }
        *target = *parsed;
    }
    if (auto value = env_value(environment, env)) {
        auto parsed = parse_uint32(*value);
        if (!parsed.has_value()) {
            if (error != nullptr) {
                *error = std::string(env) + " must be an unsigned integer";
            }
            return false;
        }
        *target = *parsed;
    }
    return true;
}

bool apply_port(
    const SectionValues& sections,
    const std::vector<EnvironmentVariable>& environment,
    const char* section,
    const char* key,
    const char* env,
    std::uint16_t* target,
    std::string* error
) {
    if (target == nullptr) {
        return true;
    }
    if (auto value = section_value(sections, section, key)) {
        auto parsed = parse_port(*value);
        if (!parsed.has_value()) {
            if (error != nullptr) {
                *error = std::string(section) + "." + key + " must be a TCP/UDP port";
            }
            return false;
        }
        *target = *parsed;
    }
    if (auto value = env_value(environment, env)) {
        auto parsed = parse_port(*value);
        if (!parsed.has_value()) {
            if (error != nullptr) {
                *error = std::string(env) + " must be a TCP/UDP port";
            }
            return false;
        }
        *target = *parsed;
    }
    return true;
}

LoadDaemonConfigResult fail(std::string error) {
    LoadDaemonConfigResult result;
    result.error = std::move(error);
    return result;
}

std::vector<std::string> args_without_program(const std::vector<std::string>& args) {
    if (args.empty()) {
        return {};
    }
    return {args.begin() + 1, args.end()};
}

} // namespace

RuntimeOptionsResult parse_runtime_options(const std::vector<std::string>& args) noexcept {
    RuntimeOptionsResult result;
    result.ok = true;
    const auto values = args_without_program(args);
    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::string& value = values[index];
        if (value == "--help" || value == "-h") {
            result.show_help = true;
            return result;
        }
        if (value == "--version") {
            result.show_version = true;
            return result;
        }
        if (value == "--check-config") {
            result.options.check_config = true;
            continue;
        }
        if (value == "--once") {
            result.options.once = true;
            continue;
        }
        if (value == "--dry-run") {
            result.options.dry_run = true;
            continue;
        }
        if (value == "--config") {
            if (index + 1 >= values.size()) {
                result.ok = false;
                result.error = "--config requires a path";
                return result;
            }
            result.options.config_path = values[++index];
            continue;
        }
        if (value.rfind("--config=", 0) == 0) {
            result.options.config_path = value.substr(9);
            continue;
        }
        result.ok = false;
        result.error = "unknown argument: " + value;
        return result;
    }
    return result;
}

LoadDaemonConfigResult load_daemon_config_from_text(
    std::string_view config_text,
    const RuntimeOptions& options,
    const std::vector<EnvironmentVariable>& environment
) noexcept {
    const SectionValues sections = parse_uci(config_text);
    DaemonConfig config;
    config.settings.state_file = "/var/lib/openomada/managed-state.json";

    std::string error;
    if (!apply_bool(sections, environment, "controller", "enabled", "OPENOMADA_ENABLED", &config.enabled, &error)) {
        return fail(error);
    }

    apply_string(sections, environment, "controller", "host", "OPENOMADA_CONTROLLER_HOST", &config.settings.controller_host);
    apply_string(sections, environment, "controller", "controller_id", "OPENOMADA_CONTROLLER_ID", &config.settings.controller_id);
    apply_string(sections, environment, "controller", "site_id", "OPENOMADA_SITE_ID", &config.settings.site_id);
    apply_string(sections, environment, "controller", "username", "OPENOMADA_DEVICE_USERNAME", &config.settings.device_username);
    apply_string(sections, environment, "controller", "password", "OPENOMADA_DEVICE_PASSWORD", &config.settings.device_password);

    apply_string(sections, environment, "agent", "state_path", "OPENOMADA_STATE_PATH", &config.settings.state_file);
    apply_string(sections, environment, "agent", "log_level", "OPENOMADA_LOG_LEVEL", &config.log_level);
    apply_string(sections, environment, "agent", "platform", "OPENOMADA_PLATFORM", &config.openwrt.platform);
    apply_string(sections, environment, "agent", "radio_bands", "OPENOMADA_RADIO_BANDS", &config.openwrt.radio_bands);
    if (!apply_bool(sections, environment, "agent", "protocol_trace", "OPENOMADA_PROTOCOL_TRACE", &config.protocol_trace, &error) ||
        !apply_uint32(sections, environment, "agent", "inform_interval_ms", "OPENOMADA_INFORM_INTERVAL_MS", &config.settings.inform_interval_ms, &error) ||
        !apply_uint32(sections, environment, "agent", "tcp_timeout_seconds", "OPENOMADA_TCP_TIMEOUT_SECONDS", &config.settings.tcp_timeout_seconds, &error) ||
        !apply_uint32(sections, environment, "agent", "reconnect_delay_ms", "OPENOMADA_RECONNECT_DELAY_MS", &config.settings.reconnect_delay_ms, &error) ||
        !apply_uint32(sections, environment, "agent", "managed_reconnect_attempts", "OPENOMADA_MANAGED_RECONNECT_ATTEMPTS", &config.settings.managed_reconnect_attempts, &error) ||
        !apply_uint32(sections, environment, "agent", "max_ssids", "OPENOMADA_MAX_SSIDS", &config.openwrt.max_ssids, &error) ||
        !apply_port(sections, environment, "agent", "discovery_port", "OPENOMADA_DISCOVERY_PORT", &config.settings.discovery_port, &error) ||
        !apply_port(sections, environment, "agent", "local_discovery_port", "OPENOMADA_LOCAL_DISCOVERY_PORT", &config.settings.local_discovery_port, &error) ||
        !apply_port(sections, environment, "agent", "manage_port", "OPENOMADA_MANAGE_PORT", &config.settings.manage_port, &error)) {
        return fail(error);
    }

    bool tls_verify = config.settings.tls_verify;
    if (!apply_bool(sections, environment, "agent", "tls_verify", "OPENOMADA_TLS_VERIFY", &tls_verify, &error)) {
        return fail(error);
    }
    config.settings.tls_verify = tls_verify;
    apply_string(sections, environment, "agent", "tls_ca_file", "OPENOMADA_TLS_CA_FILE", &config.settings.tls_ca_file);

    apply_string(sections, environment, "device", "name", "OPENOMADA_DEVICE_NAME", &config.settings.device_name);
    apply_string(sections, environment, "device", "model", "OPENOMADA_MODEL", &config.settings.model);
    apply_string(sections, environment, "device", "model_version", "OPENOMADA_MODEL_VERSION", &config.settings.model_version);
    apply_string(sections, environment, "device", "hardware_version", "OPENOMADA_HARDWARE_VERSION", &config.settings.hardware_version);
    apply_string(sections, environment, "device", "firmware_version", "OPENOMADA_FIRMWARE_VERSION", &config.settings.firmware_version);
    apply_string(sections, environment, "device", "ip", "OPENOMADA_DEVICE_IP", &config.settings.device_ip);
    if (!apply_uint32(sections, environment, "device", "customize_region", "OPENOMADA_CUSTOMIZE_REGION", &config.settings.customize_region, &error)) {
        return fail(error);
    }

    std::string raw_mac;
    apply_string(sections, environment, "device", "mac", "OPENOMADA_DEVICE_MAC", &raw_mac);
    if (!raw_mac.empty()) {
        auto parsed_mac = domain::MacAddress::parse(raw_mac);
        if (!parsed_mac.has_value()) {
            return fail("device MAC must be valid");
        }
        config.settings.mac = *parsed_mac;
    }

    apply_string(sections, environment, "openwrt", "management_vlan_interface", "OPENOMADA_MANAGEMENT_VLAN_INTERFACE", &config.openwrt.management_vlan_interface);
    apply_string(sections, environment, "openwrt", "management_vlan_device", "OPENOMADA_MANAGEMENT_VLAN_DEVICE", &config.openwrt.management_vlan_device);

    apply_string(sections, environment, "portal", "engine", "OPENOMADA_PORTAL_ENGINE", &config.openwrt.portal_engine);
    if (!apply_bool(sections, environment, "portal", "enabled", "OPENOMADA_PORTAL_ENABLED", &config.openwrt.portal_enabled, &error) ||
        !apply_bool(sections, environment, "portal", "flush_conntrack_on_deauth", "OPENOMADA_FLUSH_CONNTRACK_ON_DEAUTH", &config.openwrt.flush_conntrack_on_deauth, &error)) {
        return fail(error);
    }

    if (auto value = env_value(environment, "OPENOMADA_DEVICE_ACCOUNT_PASSWORD")) {
        config.settings.device_password = *value;
    }
    if (auto value = env_value(environment, "OPENOMADA_DESTINATION_CONTROLLER_ID")) {
        config.settings.destination_controller_id = *value;
    }

    if (config.enabled) {
        if (config.settings.controller_host.empty()) {
            return fail("controller host is required when the agent is enabled");
        }
        if (config.settings.state_file.empty()) {
            return fail("state path is required when the agent is enabled");
        }
        if (config.settings.device_password.empty()) {
            return fail("Device Account password is required when the agent is enabled");
        }
        if (raw_mac.empty()) {
            return fail("device MAC is required when the agent is enabled");
        }
    }

    (void)options;
    LoadDaemonConfigResult result;
    result.ok = true;
    result.config = std::move(config);
    return result;
}

LoadDaemonConfigResult load_daemon_config_file(
    const RuntimeOptions& options,
    const std::vector<EnvironmentVariable>& environment
) noexcept {
    std::ifstream input(options.config_path);
    if (!input.good()) {
        return fail("could not open config file: " + options.config_path + ": " + std::strerror(errno));
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return load_daemon_config_from_text(buffer.str(), options, environment);
}

std::string daemon_config_summary(const DaemonConfig& config) {
    std::string out = "enabled=";
    out += config.enabled ? "1" : "0";
    out += " controller=";
    out += config.settings.controller_host.empty() ? "<unset>" : config.settings.controller_host;
    out += " site=";
    out += config.settings.site_id.empty() ? "<unset>" : config.settings.site_id;
    out += " mac=";
    out += config.settings.mac.omada();
    out += " model=";
    out += config.settings.model;
    out += " state=";
    out += config.settings.state_file.empty() ? "<unset>" : config.settings.state_file;
    out += " informMs=";
    out += std::to_string(config.settings.inform_interval_ms);
    out += " platform=";
    out += config.openwrt.platform;
    out += " bands=";
    out += config.openwrt.radio_bands;
    out += " maxSsids=";
    out += std::to_string(config.openwrt.max_ssids);
    out += " portal=";
    out += config.openwrt.portal_enabled ? config.openwrt.portal_engine : "disabled";
    return out;
}

} // namespace openomada::application
