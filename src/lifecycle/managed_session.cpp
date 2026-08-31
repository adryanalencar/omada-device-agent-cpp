#include "openomada/lifecycle/managed_session.hpp"

#include <algorithm>

namespace openomada::lifecycle {

InformScheduler::InformScheduler(std::uint32_t interval_ms)
    : interval_ms_(std::max<std::uint32_t>(500, interval_ms)) {}

void InformScheduler::start(std::uint32_t last_seq, std::uint64_t started_at_ms) noexcept {
    started_ = true;
    initial_sent_ = false;
    next_seq_ = last_seq + 1U;
    started_at_ms_ = started_at_ms;
    next_due_ms_ = started_at_ms;
}

ScheduledInform InformScheduler::poll(std::uint64_t now_ms) noexcept {
    if (!started_ || now_ms < next_due_ms_) {
        return {};
    }

    ScheduledInform scheduled;
    scheduled.due = true;
    scheduled.need_reply = !initial_sent_;
    scheduled.seq = next_seq_;
    scheduled.uptime_seconds = now_ms > started_at_ms_ ? (now_ms - started_at_ms_) / 1000U : 0U;

    ++next_seq_;
    initial_sent_ = true;
    next_due_ms_ = now_ms + interval_ms_;
    scheduled.next_due_ms = next_due_ms_;
    return scheduled;
}

transport::TransportStatus send_inform(
    transport::FrameTransport& transport,
    const application::AgentSettings& settings,
    const domain::AccessPointProfile& profile,
    application::InformProvider& provider,
    const std::string& controller_id,
    const ScheduledInform& scheduled,
    std::uint64_t timestamp_ms
) {
    if (!scheduled.due) {
        return transport::TransportStatus::success();
    }
    const auto snapshot = provider.build(scheduled.need_reply, scheduled.uptime_seconds);
    return transport.send_payload(application::build_inform_request_json(
        settings,
        profile,
        scheduled.seq,
        controller_id,
        snapshot,
        timestamp_ms
    ));
}

ManagedState managed_state_from_adoption(
    const application::AgentSettings& settings,
    std::uint16_t manage_port,
    const AdoptionResult& result,
    std::uint64_t updated_at
) {
    ManagedState state;
    state.version = 1;
    state.mac = settings.mac.normalized();
    state.controller_host = settings.controller_host;
    state.controller_id = result.controller_id;
    state.manage_port = manage_port;
    state.site_id = settings.site_id;
    state.username = result.username;
    state.config_version = result.config_version;
    state.sequence_id = result.sequence_id;
    state.updated_at = updated_at;
    return state;
}

} // namespace openomada::lifecycle
