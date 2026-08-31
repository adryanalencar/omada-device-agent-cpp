#include "openomada/lifecycle/session.hpp"

namespace openomada::lifecycle {

const char* to_string(LifecycleState state) noexcept {
    switch (state) {
    case LifecycleState::Discovering:
        return "discovering";
    case LifecycleState::Adopting:
        return "adopting";
    case LifecycleState::Verifying:
        return "verifying";
    case LifecycleState::Negotiating:
        return "negotiating";
    case LifecycleState::Managed:
        return "managed";
    case LifecycleState::Rebuilding:
        return "rebuilding";
    case LifecycleState::Disconnected:
        return "disconnected";
    }
    return "unknown";
}

bool can_transition(LifecycleState from, LifecycleState to) noexcept {
    switch (from) {
    case LifecycleState::Disconnected:
        return to == LifecycleState::Discovering || to == LifecycleState::Rebuilding;
    case LifecycleState::Discovering:
        return to == LifecycleState::Adopting || to == LifecycleState::Disconnected;
    case LifecycleState::Adopting:
        return to == LifecycleState::Verifying || to == LifecycleState::Disconnected;
    case LifecycleState::Verifying:
        return to == LifecycleState::Negotiating || to == LifecycleState::Disconnected;
    case LifecycleState::Negotiating:
        return to == LifecycleState::Managed || to == LifecycleState::Disconnected;
    case LifecycleState::Managed:
        return to == LifecycleState::Rebuilding || to == LifecycleState::Disconnected;
    case LifecycleState::Rebuilding:
        return to == LifecycleState::Verifying ||
               to == LifecycleState::Discovering ||
               to == LifecycleState::Disconnected;
    }
    return false;
}

bool ControllerSession::transition(LifecycleState target) noexcept {
    if (!can_transition(state, target)) {
        return false;
    }
    state = target;
    return true;
}

} // namespace openomada::lifecycle

