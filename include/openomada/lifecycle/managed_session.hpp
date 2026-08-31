#pragma once

#include <cstdint>

#include "openomada/application/inform.hpp"
#include "openomada/application/settings.hpp"
#include "openomada/domain/device_profile.hpp"
#include "openomada/lifecycle/adoption.hpp"
#include "openomada/lifecycle/session.hpp"
#include "openomada/transport/frame_transport.hpp"

namespace openomada::lifecycle {

struct ScheduledInform {
    bool due{false};
    bool need_reply{false};
    std::uint32_t seq{0};
    std::uint64_t uptime_seconds{0};
    std::uint64_t next_due_ms{0};
};

class InformScheduler {
public:
    explicit InformScheduler(std::uint32_t interval_ms);

    void start(std::uint32_t last_seq, std::uint64_t started_at_ms) noexcept;
    ScheduledInform poll(std::uint64_t now_ms) noexcept;

    std::uint32_t interval_ms() const noexcept { return interval_ms_; }

private:
    std::uint32_t interval_ms_{3000};
    bool started_{false};
    bool initial_sent_{false};
    std::uint32_t next_seq_{1};
    std::uint64_t started_at_ms_{0};
    std::uint64_t next_due_ms_{0};
};

transport::TransportStatus send_inform(
    transport::FrameTransport& transport,
    const application::AgentSettings& settings,
    const domain::AccessPointProfile& profile,
    application::InformProvider& provider,
    const std::string& controller_id,
    const ScheduledInform& scheduled,
    std::uint64_t timestamp_ms = 0
);

ManagedState managed_state_from_adoption(
    const application::AgentSettings& settings,
    std::uint16_t manage_port,
    const AdoptionResult& result,
    std::uint64_t updated_at
);

} // namespace openomada::lifecycle
