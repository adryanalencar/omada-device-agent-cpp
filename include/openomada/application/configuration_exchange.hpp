#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "openomada/application/configuration.hpp"
#include "openomada/application/settings.hpp"
#include "openomada/protocol/message_type.hpp"

namespace openomada::application {

constexpr std::int64_t kConfigOk = 0;
constexpr std::int64_t kConfigError = 1;

struct SetRequestEvaluation {
    bool parsed{false};
    AccessPointConfigUpdate update{};
    std::int64_t errcode{kConfigError};
    std::string error{};
};

struct SetResponseBodyResult {
    bool ok{false};
    std::int64_t sequence_id{0};
    std::int64_t config_version{0};
    std::int64_t errcode{kConfigError};
    std::string body_json{};
    std::string error{};
};

struct GetResponseBodyResult {
    bool ok{false};
    std::int64_t sequence_id{0};
    std::string body_json{};
    std::string error{};
};

struct NotifyReplyResult {
    bool ok{false};
    bool should_reply{false};
    protocol::MessageType response_type{protocol::MessageType::NotifyReply};
    std::string body_json{};
    std::string error{};
};

struct PortalAuthReplyResult {
    bool ok{false};
    bool should_reply{false};
    std::optional<std::int64_t> sequence_id{};
    std::int64_t errcode{kConfigError};
    std::string body_json{};
    std::string error{};
};

struct ForgetResponseResult {
    bool ok{false};
    protocol::MessageType response_type{protocol::MessageType::ForgetResponse};
    std::string message_json{};
    std::string error{};
};

SetRequestEvaluation evaluate_set_request(std::string_view message_json) noexcept;

SetResponseBodyResult build_set_response_body_json(
    std::string_view request_json,
    std::optional<std::int64_t> current_config_version,
    std::int64_t errcode = kConfigOk
) noexcept;

std::string build_set_response_json(
    const AgentSettings& settings,
    std::string_view request_json,
    const std::string& controller_id,
    const SetResponseBodyResult& body,
    std::uint64_t timestamp_ms = 0
);

GetResponseBodyResult build_get_response_body_json(
    std::string_view request_json,
    std::int64_t errcode = kConfigError
) noexcept;

std::string build_get_response_json(
    const AgentSettings& settings,
    std::string_view request_json,
    const std::string& controller_id,
    const GetResponseBodyResult& body,
    std::uint64_t timestamp_ms = 0
);

NotifyReplyResult build_notify_reply_body_json(
    std::string_view request_json,
    std::int64_t errcode = kConfigError
) noexcept;

std::string build_notify_reply_json(
    const AgentSettings& settings,
    std::string_view request_json,
    const std::string& controller_id,
    const NotifyReplyResult& reply,
    std::uint64_t timestamp_ms = 0
);

PortalAuthReplyResult build_portal_auth_reply_body_json(
    std::string_view request_json,
    std::int64_t errcode = kConfigOk
) noexcept;

std::string build_portal_auth_reply_json(
    const AgentSettings& settings,
    std::string_view request_json,
    const std::string& controller_id,
    const PortalAuthReplyResult& reply,
    std::uint64_t timestamp_ms = 0
);

ForgetResponseResult build_forget_response_json(
    const AgentSettings& settings,
    std::string_view request_json,
    const std::string& controller_id,
    std::uint64_t timestamp_ms = 0
) noexcept;

} // namespace openomada::application
