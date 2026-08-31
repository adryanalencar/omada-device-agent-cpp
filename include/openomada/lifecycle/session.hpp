#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace openomada::lifecycle {

enum class LifecycleState {
    Discovering,
    Adopting,
    Verifying,
    Negotiating,
    Managed,
    Rebuilding,
    Disconnected,
};

const char* to_string(LifecycleState state) noexcept;
bool can_transition(LifecycleState from, LifecycleState to) noexcept;

struct ControllerSession {
    LifecycleState state{LifecycleState::Disconnected};
    std::optional<std::uint32_t> config_version{};
    std::optional<std::uint32_t> sequence_id{};

    bool transition(LifecycleState target) noexcept;
};

struct ManagedState {
    std::uint32_t version{1};
    std::string mac{};
    std::string controller_host{};
    std::string controller_id{};
    std::uint16_t manage_port{29814};
    std::string site_id{};
    std::string username{};
    std::optional<std::uint32_t> config_version{};
    std::optional<std::uint32_t> sequence_id{};
    std::uint64_t updated_at{0};
};

} // namespace openomada::lifecycle

