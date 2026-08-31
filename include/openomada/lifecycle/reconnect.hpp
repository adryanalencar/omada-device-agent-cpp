#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "openomada/application/settings.hpp"
#include "openomada/lifecycle/adoption.hpp"
#include "openomada/lifecycle/session.hpp"
#include "openomada/persistence/session_state_repository.hpp"
#include "openomada/transport/frame_transport.hpp"

namespace openomada::lifecycle {

enum class ManagedReconnectOutcome {
    NoState,
    StateLoadFailed,
    DirectReconnectSucceeded,
    DirectReconnectExhausted,
    RediscoverySent,
    RediscoveryFailed,
};

struct ManagedReconnectOptions {
    std::uint32_t max_direct_attempts{3};
};

struct ManagedReconnectResult {
    ManagedReconnectOutcome outcome{ManagedReconnectOutcome::NoState};
    std::uint32_t attempts{0};
    ManagedState state{};
    AdoptionResult adoption{};
    std::string error{};
};

using ManagedAdoptionAttempt = std::function<AdoptionResult(const ManagedState&)>;
using ManagedRediscoveryAttempt = std::function<transport::TransportStatus(const ManagedState&)>;

const char* to_string(ManagedReconnectOutcome outcome) noexcept;

application::AgentSettings settings_for_managed_state(
    const application::AgentSettings& settings,
    const ManagedState& state
);

AdoptionOptions adoption_options_for_managed_state(const ManagedState& state);

ManagedReconnectResult run_managed_reconnect(
    const persistence::SessionStateRepository& repository,
    const ManagedReconnectOptions& options,
    const ManagedAdoptionAttempt& attempt_adoption,
    const ManagedRediscoveryAttempt& send_rediscovery
);

} // namespace openomada::lifecycle
