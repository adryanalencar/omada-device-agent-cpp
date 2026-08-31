#pragma once

#include <cstdint>
#include <string>

#include "openomada/application/settings.hpp"
#include "openomada/domain/device_profile.hpp"

namespace openomada::application {

struct LanObservation {
    std::string rate{"100.0"};
    std::int32_t duplex{1};
    std::string port{"LAN"};
};

struct InformSnapshot {
    bool need_reply{false};
    std::uint64_t uptime_seconds{0};
    LanObservation lan{};
};

class InformProvider {
public:
    virtual ~InformProvider() = default;
    virtual InformSnapshot build(bool need_reply, std::uint64_t uptime_seconds) = 0;
};

class StaticInformProvider final : public InformProvider {
public:
    explicit StaticInformProvider(LanObservation lan = {});

    InformSnapshot build(bool need_reply, std::uint64_t uptime_seconds) override;

private:
    LanObservation lan_;
};

std::string build_inform_body_json(
    const domain::AccessPointProfile& profile,
    const InformSnapshot& snapshot
);

std::string build_inform_request_json(
    const AgentSettings& settings,
    const domain::AccessPointProfile& profile,
    std::uint32_t seq,
    const std::string& controller_id,
    const InformSnapshot& snapshot,
    std::uint64_t timestamp_ms = 0
);

} // namespace openomada::application
