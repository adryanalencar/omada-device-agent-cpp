#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "openomada/application/settings.hpp"
#include "openomada/domain/device_profile.hpp"

namespace openomada::protocol {

std::string build_discovery_json(
    const application::AgentSettings& settings,
    const domain::AccessPointProfile& profile,
    std::uint32_t seq,
    bool managed_restart = false,
    std::uint64_t timestamp_ms = 0
);

std::string build_preconnect_json(
    const application::AgentSettings& settings,
    const domain::AccessPointProfile& profile,
    std::uint32_t seq,
    const std::string& controller_id,
    bool managed_reconnect,
    std::uint64_t timestamp_ms = 0
);

std::string build_device_verify_json(
    const application::AgentSettings& settings,
    std::uint32_t seq,
    const std::string& controller_id,
    const std::string& auth,
    const std::string& random_key_for_system_verify,
    std::uint64_t timestamp_ms = 0
);

std::string build_system_verify_result_json(
    const application::AgentSettings& settings,
    std::uint32_t seq,
    const std::string& controller_id,
    std::uint64_t timestamp_ms = 0
);

std::string build_device_negotiation_json(
    const application::AgentSettings& settings,
    const domain::AccessPointProfile& profile,
    std::uint32_t seq,
    const std::string& controller_id,
    std::uint32_t config_version,
    std::uint64_t timestamp_ms = 0
);

std::string build_init_sync_result_json(
    const application::AgentSettings& settings,
    std::uint32_t seq,
    const std::string& controller_id,
    std::uint64_t timestamp_ms = 0
);

} // namespace openomada::protocol

