#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "openomada/application/configuration_applier.hpp"
#include "openomada/application/settings.hpp"
#include "openomada/lifecycle/session.hpp"
#include "openomada/protocol/message_type.hpp"
#include "openomada/transport/frame_transport.hpp"

namespace openomada::lifecycle {

enum class ManagedRequestAction {
    Ignored,
    SetResponse,
    GetResponse,
    NotifyReply,
    NotifyNoReply,
    ForgetResponse,
};

struct ManagedRequestResult {
    bool ok{false};
    ManagedRequestAction action{ManagedRequestAction::Ignored};
    protocol::MessageType request_type{protocol::MessageType::Discovery};
    std::optional<protocol::MessageType> response_type{};
    bool response_sent{false};
    bool should_clear_state{false};
    bool should_end_session{false};
    bool configuration_applied{false};
    bool configuration_changed{false};
    std::optional<std::int64_t> config_version{};
    std::optional<std::int64_t> sequence_id{};
    std::string error{};
};

const char* to_string(ManagedRequestAction action) noexcept;

ManagedRequestResult handle_managed_request(
    transport::FrameTransport& transport,
    const application::AgentSettings& settings,
    ManagedState* state,
    std::string_view payload,
    std::uint64_t timestamp_ms = 0
);

ManagedRequestResult handle_managed_request(
    transport::FrameTransport& transport,
    const application::AgentSettings& settings,
    ManagedState* state,
    std::string_view payload,
    application::ConfigurationApplier* applier,
    std::uint64_t timestamp_ms = 0
);

} // namespace openomada::lifecycle
