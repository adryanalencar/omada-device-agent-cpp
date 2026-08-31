#include "openomada/lifecycle/managed_request_handler.hpp"

#include "openomada/application/configuration_exchange.hpp"
#include "openomada/protocol/json.hpp"

#include <limits>
#include <utility>

namespace openomada::lifecycle {
namespace {

ManagedRequestResult failed(protocol::MessageType request_type, std::string error) {
    ManagedRequestResult result;
    result.request_type = request_type;
    result.error = std::move(error);
    return result;
}

std::optional<protocol::MessageType> message_type_from_value(std::uint32_t value) {
    switch (value) {
    case protocol::to_underlying(protocol::MessageType::SetRequest):
        return protocol::MessageType::SetRequest;
    case protocol::to_underlying(protocol::MessageType::GetRequest):
        return protocol::MessageType::GetRequest;
    case protocol::to_underlying(protocol::MessageType::NotifyRequest):
        return protocol::MessageType::NotifyRequest;
    case protocol::to_underlying(protocol::MessageType::NotifyRequestV2):
        return protocol::MessageType::NotifyRequestV2;
    case protocol::to_underlying(protocol::MessageType::ForgetRequest):
        return protocol::MessageType::ForgetRequest;
    case protocol::to_underlying(protocol::MessageType::ForgetRequestNoReset):
        return protocol::MessageType::ForgetRequestNoReset;
    default:
        return std::nullopt;
    }
}

transport::TransportStatus send_payload(
    transport::FrameTransport& transport,
    std::string_view payload
) {
    return transport.send_payload(payload);
}

} // namespace

const char* to_string(ManagedRequestAction action) noexcept {
    switch (action) {
    case ManagedRequestAction::Ignored:
        return "ignored";
    case ManagedRequestAction::SetResponse:
        return "set-response";
    case ManagedRequestAction::GetResponse:
        return "get-response";
    case ManagedRequestAction::NotifyReply:
        return "notify-reply";
    case ManagedRequestAction::NotifyNoReply:
        return "notify-no-reply";
    case ManagedRequestAction::ForgetResponse:
        return "forget-response";
    }
    return "unknown";
}

ManagedRequestResult handle_managed_request(
    transport::FrameTransport& transport,
    const application::AgentSettings& settings,
    ManagedState* state,
    std::string_view payload,
    std::uint64_t timestamp_ms
) {
    if (state == nullptr) {
        return failed(protocol::MessageType::Discovery, "managed state is required");
    }

    auto document = protocol::JsonDocument::parse(payload);
    if (!document.valid()) {
        return failed(protocol::MessageType::Discovery, "managed request must be a JSON object");
    }
    auto type_value = protocol::ecsp_header_type(document.get());
    if (!type_value.has_value()) {
        return failed(protocol::MessageType::Discovery, "managed request is missing header.type");
    }
    auto request_type = message_type_from_value(*type_value);
    if (!request_type.has_value()) {
        ManagedRequestResult ignored;
        ignored.ok = true;
        ignored.action = ManagedRequestAction::Ignored;
        return ignored;
    }

    ManagedRequestResult result;
    result.request_type = *request_type;

    if (*request_type == protocol::MessageType::SetRequest) {
        const auto evaluation = application::evaluate_set_request(payload);
        const auto body = application::build_set_response_body_json(
            payload,
            state->config_version.has_value()
                ? std::optional<std::int64_t>(*state->config_version)
                : std::nullopt,
            evaluation.errcode
        );
        if (!body.ok) {
            return failed(*request_type, body.error);
        }
        const std::string response = application::build_set_response_json(
            settings,
            payload,
            state->controller_id,
            body,
            timestamp_ms
        );
        const auto send_status = send_payload(transport, response);
        if (!send_status.ok) {
            return failed(*request_type, send_status.error);
        }
        result.ok = true;
        result.action = ManagedRequestAction::SetResponse;
        result.response_type = protocol::MessageType::SetResponse;
        result.response_sent = true;
        result.sequence_id = body.sequence_id;
        result.config_version = body.config_version;
        result.error = evaluation.error;
        if (evaluation.errcode == application::kConfigOk) {
            if (body.config_version >= 0 &&
                body.config_version <= std::numeric_limits<std::uint32_t>::max()) {
                state->config_version = static_cast<std::uint32_t>(body.config_version);
            }
            if (body.sequence_id >= 0 &&
                body.sequence_id <= std::numeric_limits<std::uint32_t>::max()) {
                state->sequence_id = static_cast<std::uint32_t>(body.sequence_id);
            }
        }
        return result;
    }

    if (*request_type == protocol::MessageType::GetRequest) {
        const auto body = application::build_get_response_body_json(payload);
        if (!body.ok) {
            return failed(*request_type, body.error);
        }
        const std::string response = application::build_get_response_json(
            settings,
            payload,
            state->controller_id,
            body,
            timestamp_ms
        );
        const auto send_status = send_payload(transport, response);
        if (!send_status.ok) {
            return failed(*request_type, send_status.error);
        }
        result.ok = true;
        result.action = ManagedRequestAction::GetResponse;
        result.response_type = protocol::MessageType::GetResponse;
        result.response_sent = true;
        result.sequence_id = body.sequence_id;
        return result;
    }

    if (*request_type == protocol::MessageType::NotifyRequest ||
        *request_type == protocol::MessageType::NotifyRequestV2) {
        const auto reply = application::build_notify_reply_body_json(payload);
        if (!reply.ok) {
            return failed(*request_type, reply.error);
        }
        result.ok = true;
        result.action = reply.should_reply
            ? ManagedRequestAction::NotifyReply
            : ManagedRequestAction::NotifyNoReply;
        result.response_type = reply.response_type;
        if (!reply.should_reply) {
            return result;
        }
        const std::string response = application::build_notify_reply_json(
            settings,
            payload,
            state->controller_id,
            reply,
            timestamp_ms
        );
        const auto send_status = send_payload(transport, response);
        if (!send_status.ok) {
            return failed(*request_type, send_status.error);
        }
        result.response_sent = true;
        return result;
    }

    if (*request_type == protocol::MessageType::ForgetRequest ||
        *request_type == protocol::MessageType::ForgetRequestNoReset) {
        const auto response = application::build_forget_response_json(
            settings,
            payload,
            state->controller_id,
            timestamp_ms
        );
        if (!response.ok) {
            return failed(*request_type, response.error);
        }
        const auto send_status = send_payload(transport, response.message_json);
        if (!send_status.ok) {
            return failed(*request_type, send_status.error);
        }
        result.ok = true;
        result.action = ManagedRequestAction::ForgetResponse;
        result.response_type = response.response_type;
        result.response_sent = true;
        result.should_clear_state = true;
        result.should_end_session = true;
        return result;
    }

    result.ok = true;
    result.action = ManagedRequestAction::Ignored;
    return result;
}

} // namespace openomada::lifecycle
