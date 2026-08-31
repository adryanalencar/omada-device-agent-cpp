#include "openomada/lifecycle/reconnect.hpp"

#include <algorithm>
#include <utility>

namespace openomada::lifecycle {

const char* to_string(ManagedReconnectOutcome outcome) noexcept {
    switch (outcome) {
    case ManagedReconnectOutcome::NoState:
        return "no-state";
    case ManagedReconnectOutcome::StateLoadFailed:
        return "state-load-failed";
    case ManagedReconnectOutcome::DirectReconnectSucceeded:
        return "direct-reconnect-succeeded";
    case ManagedReconnectOutcome::DirectReconnectExhausted:
        return "direct-reconnect-exhausted";
    case ManagedReconnectOutcome::RediscoverySent:
        return "rediscovery-sent";
    case ManagedReconnectOutcome::RediscoveryFailed:
        return "rediscovery-failed";
    }
    return "unknown";
}

application::AgentSettings settings_for_managed_state(
    const application::AgentSettings& settings,
    const ManagedState& state
) {
    auto managed = settings;
    managed.controller_host = state.controller_host;
    managed.controller_id = state.controller_id;
    managed.manage_port = state.manage_port;
    if (!state.site_id.empty()) {
        managed.site_id = state.site_id;
    }
    if (managed.device_username.empty() && !state.username.empty()) {
        managed.device_username = state.username;
    }
    return managed;
}

AdoptionOptions adoption_options_for_managed_state(const ManagedState& state) {
    AdoptionOptions options;
    options.managed_reconnect = true;
    options.known_config_version = state.config_version;
    return options;
}

ManagedReconnectResult run_managed_reconnect(
    const persistence::SessionStateRepository& repository,
    const ManagedReconnectOptions& options,
    const ManagedAdoptionAttempt& attempt_adoption,
    const ManagedRediscoveryAttempt& send_rediscovery
) {
    ManagedReconnectResult result;
    const auto loaded = repository.load();
    if (!loaded.ok) {
        result.outcome = ManagedReconnectOutcome::StateLoadFailed;
        result.error = loaded.error;
        return result;
    }
    if (!loaded.found) {
        result.outcome = ManagedReconnectOutcome::NoState;
        result.error = loaded.error;
        return result;
    }

    result.state = loaded.state;
    const std::uint32_t attempts = options.max_direct_attempts;
    for (std::uint32_t index = 0; index < attempts; ++index) {
        ++result.attempts;
        result.adoption = attempt_adoption(result.state);
        if (result.adoption.ok && result.adoption.initial_sync_complete) {
            result.outcome = ManagedReconnectOutcome::DirectReconnectSucceeded;
            result.error.clear();
            return result;
        }
        result.error = result.adoption.detail;
        if (result.error.empty()) {
            result.error = to_string(result.adoption.error);
        }
    }

    if (!send_rediscovery) {
        result.outcome = ManagedReconnectOutcome::DirectReconnectExhausted;
        return result;
    }

    auto rediscovery = send_rediscovery(result.state);
    if (rediscovery.ok) {
        result.outcome = ManagedReconnectOutcome::RediscoverySent;
        result.error.clear();
        return result;
    }
    result.outcome = ManagedReconnectOutcome::RediscoveryFailed;
    result.error = std::move(rediscovery.error);
    return result;
}

} // namespace openomada::lifecycle
