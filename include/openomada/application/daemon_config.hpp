#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "openomada/application/settings.hpp"

namespace openomada::application {

struct RuntimeOptions {
    std::string config_path{"/etc/config/openomada"};
    bool check_config{false};
    bool once{false};
    bool dry_run{false};
};

struct OpenWrtRuntimeConfig {
    std::string platform{"auto"};
    std::string radio_bands{"2g"};
    std::uint32_t max_ssids{4};
    std::string management_vlan_interface{};
    std::string management_vlan_device{};
    bool portal_enabled{true};
    std::string portal_engine{"opennds"};
    bool flush_conntrack_on_deauth{true};
};

struct DaemonConfig {
    bool enabled{false};
    bool protocol_trace{false};
    std::string log_level{"info"};
    AgentSettings settings{};
    OpenWrtRuntimeConfig openwrt{};
};

struct EnvironmentVariable {
    std::string name{};
    std::string value{};
};

struct LoadDaemonConfigResult {
    bool ok{false};
    DaemonConfig config{};
    std::string error{};
};

struct RuntimeOptionsResult {
    bool ok{false};
    RuntimeOptions options{};
    bool show_help{false};
    bool show_version{false};
    std::string error{};
};

RuntimeOptionsResult parse_runtime_options(const std::vector<std::string>& args) noexcept;

LoadDaemonConfigResult load_daemon_config_from_text(
    std::string_view config_text,
    const RuntimeOptions& options,
    const std::vector<EnvironmentVariable>& environment = {}
) noexcept;

LoadDaemonConfigResult load_daemon_config_file(
    const RuntimeOptions& options,
    const std::vector<EnvironmentVariable>& environment = {}
) noexcept;

std::string daemon_config_summary(const DaemonConfig& config);

} // namespace openomada::application
