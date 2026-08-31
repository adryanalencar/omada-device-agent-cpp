#include "openomada/application/configuration_exchange.hpp"

#include "openomada/protocol/ecsp_message.hpp"
#include "openomada/protocol/json.hpp"

#include <algorithm>
#include <json-c/json.h>
#include <string>
#include <utility>
#include <vector>

namespace openomada::application {
namespace {

struct RequestFields {
    bool ok{false};
    std::optional<std::uint32_t> header_seq{};
    std::uint32_t header_type{0};
    std::optional<std::int64_t> sequence_id{};
    std::optional<std::int64_t> config_version{};
    std::optional<std::int64_t> config_version_inc{};
    std::string error{};
    protocol::JsonDocument document{};
};

struct HeaderFields {
    bool ok{false};
    std::optional<std::uint32_t> header_seq{};
    std::uint32_t header_type{0};
    std::string error{};
    protocol::JsonDocument document{};
};

std::optional<std::int64_t> optional_int(json_object* value) {
    if (value == nullptr || json_object_is_type(value, json_type_null)) {
        return std::nullopt;
    }
    if (json_object_is_type(value, json_type_string)) {
        const char* text = json_object_get_string(value);
        if (text == nullptr || *text == '\0') {
            return std::nullopt;
        }
        char* end = nullptr;
        const long long parsed = std::strtoll(text, &end, 10);
        if (end == text || (end != nullptr && *end != '\0')) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(parsed);
    }
    if (!json_object_is_type(value, json_type_int) &&
        !json_object_is_type(value, json_type_boolean) &&
        !json_object_is_type(value, json_type_double)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(json_object_get_int64(value));
}

std::string quoted(std::string_view value) {
    std::string out = "\"";
    out += protocol::json_escape(value);
    out += "\"";
    return out;
}

RequestFields parse_request(std::string_view request_json) {
    RequestFields fields;
    fields.document = protocol::JsonDocument::parse(request_json);
    if (!fields.document.valid()) {
        fields.error = "request must be a JSON object";
        return fields;
    }
    fields.header_seq = protocol::ecsp_header_seq(fields.document.get());
    fields.header_type = protocol::ecsp_header_type(fields.document.get()).value_or(0);
    json_object* body = protocol::ecsp_body(fields.document.get());
    if (body == nullptr || !json_object_is_type(body, json_type_object)) {
        fields.error = "request body must be a JSON object";
        return fields;
    }
    fields.sequence_id = optional_int(protocol::object_member(body, "sequenceId"));
    fields.config_version = optional_int(protocol::object_member(body, "configVersion"));
    fields.config_version_inc = optional_int(protocol::object_member(body, "configVersionInc"));
    fields.ok = true;
    return fields;
}

HeaderFields parse_header(std::string_view request_json) {
    HeaderFields fields;
    fields.document = protocol::JsonDocument::parse(request_json);
    if (!fields.document.valid()) {
        fields.error = "request must be a JSON object";
        return fields;
    }
    fields.header_seq = protocol::ecsp_header_seq(fields.document.get());
    fields.header_type = protocol::ecsp_header_type(fields.document.get()).value_or(0);
    fields.ok = true;
    return fields;
}

protocol::EcspHeader response_header(
    const AgentSettings& settings,
    protocol::MessageType type,
    std::optional<std::uint32_t> seq,
    const std::string& controller_id,
    std::uint64_t timestamp_ms
) {
    protocol::EcspHeader header{
        seq,
        settings.ecsp_version,
        settings.ecsp_ver_cap,
        "ap",
        settings.mac,
        type,
        0,
        controller_id,
        std::nullopt,
    };
    if (timestamp_ms != 0) {
        header.timestamp = timestamp_ms;
    }
    return header;
}

std::string get_body_json(json_object* body, std::int64_t sequence_id, std::int64_t errcode) {
    std::vector<std::string> keys;
    json_object_object_foreach(body, key, value) {
        (void)value;
        const std::string key_name(key);
        if (key_name != "sequenceId") {
            keys.push_back(key_name);
        }
    }
    std::sort(keys.begin(), keys.end());

    std::string out = "{\"sequenceId\":";
    out += std::to_string(sequence_id);
    out += ",\"errcode\":";
    out += std::to_string(errcode);
    if (!keys.empty()) {
        out += ",\"unsupportedKeys\":[";
        for (std::size_t index = 0; index < keys.size(); ++index) {
            if (index != 0) {
                out.push_back(',');
            }
            out += quoted(keys[index]);
        }
        out.push_back(']');
    }
    out.push_back('}');
    return out;
}

bool body_has_no_reply(json_object* body) {
    return optional_int(protocol::object_member(body, "nre")).value_or(0) == 1;
}

std::string notify_body_json(json_object* body, std::int64_t errcode) {
    std::string out = "{\"err\":";
    out += std::to_string(errcode);
    if (auto nid = optional_int(protocol::object_member(body, "nid"))) {
        out += ",\"nid\":";
        out += std::to_string(*nid);
    }
    if (auto sub = optional_int(protocol::object_member(body, "sub"))) {
        out += ",\"sub\":";
        out += std::to_string(*sub);
    }
    if (errcode != kConfigOk) {
        out += ",\"rst\":{\"error\":\"unsupported notify request\"}";
    }
    out.push_back('}');
    return out;
}

SetResponseBodyResult set_response_error(std::string message) {
    SetResponseBodyResult result;
    result.error = std::move(message);
    result.errcode = kConfigError;
    return result;
}

GetResponseBodyResult get_response_error(std::string message) {
    GetResponseBodyResult result;
    result.error = std::move(message);
    return result;
}

NotifyReplyResult notify_error(std::string message) {
    NotifyReplyResult result;
    result.error = std::move(message);
    return result;
}

PortalAuthReplyResult portal_auth_error(std::string message) {
    PortalAuthReplyResult result;
    result.error = std::move(message);
    return result;
}

} // namespace

SetRequestEvaluation evaluate_set_request(std::string_view message_json) noexcept {
    SetRequestEvaluation evaluation;
    auto parsed = parse_set_request_json(message_json);
    evaluation.parsed = parsed.ok;
    evaluation.update = std::move(parsed.update);
    if (!parsed.ok) {
        evaluation.errcode = kConfigError;
        evaluation.error = std::move(parsed.error);
        return evaluation;
    }
    if (!evaluation.update.unhandled_keys.empty()) {
        evaluation.errcode = kConfigError;
        evaluation.error = "unsupported keys: ";
        for (std::size_t index = 0; index < evaluation.update.unhandled_keys.size(); ++index) {
            if (index != 0) {
                evaluation.error.push_back(',');
            }
            evaluation.error += evaluation.update.unhandled_keys[index];
        }
        return evaluation;
    }
    if (is_actionable_config(evaluation.update)) {
        evaluation.errcode = kConfigError;
        evaluation.error = "actionable AP configuration requires a platform applier";
        return evaluation;
    }
    evaluation.errcode = kConfigOk;
    return evaluation;
}

SetResponseBodyResult build_set_response_body_json(
    std::string_view request_json,
    std::optional<std::int64_t> current_config_version,
    std::int64_t errcode
) noexcept {
    auto fields = parse_request(request_json);
    if (!fields.ok) {
        return set_response_error(fields.error);
    }
    if (!fields.sequence_id.has_value()) {
        return set_response_error("SET_REQUEST is missing sequenceId");
    }

    std::int64_t applied_version = 0;
    if (fields.config_version.has_value()) {
        applied_version = *fields.config_version;
    } else if (fields.config_version_inc.has_value()) {
        if (!current_config_version.has_value()) {
            return set_response_error("SET_REQUEST uses configVersionInc but the local config version is unknown");
        }
        if (*fields.config_version_inc < 0) {
            return set_response_error("SET_REQUEST has invalid negative configVersionInc");
        }
        applied_version = *current_config_version + *fields.config_version_inc;
    } else {
        return set_response_error("SET_REQUEST has neither configVersion nor configVersionInc");
    }

    const std::int64_t response_version =
        errcode != kConfigOk && current_config_version.has_value()
            ? *current_config_version
            : applied_version;

    SetResponseBodyResult result;
    result.ok = true;
    result.sequence_id = *fields.sequence_id;
    result.errcode = errcode;
    result.config_version = response_version;
    result.body_json = "{\"sequenceId\":";
    result.body_json += std::to_string(result.sequence_id);
    result.body_json += ",\"errcode\":";
    result.body_json += std::to_string(result.errcode);
    result.body_json += ",\"configVersion\":";
    result.body_json += std::to_string(result.config_version);
    result.body_json += "}";
    return result;
}

std::string build_set_response_json(
    const AgentSettings& settings,
    std::string_view request_json,
    const std::string& controller_id,
    const SetResponseBodyResult& body,
    std::uint64_t timestamp_ms
) {
    const auto fields = parse_request(request_json);
    return protocol::build_message_json(
        response_header(settings, protocol::MessageType::SetResponse, fields.header_seq, controller_id, timestamp_ms),
        body.body_json
    );
}

GetResponseBodyResult build_get_response_body_json(
    std::string_view request_json,
    std::int64_t errcode
) noexcept {
    auto fields = parse_request(request_json);
    if (!fields.ok) {
        return get_response_error(fields.error);
    }
    if (!fields.sequence_id.has_value()) {
        return get_response_error("GET_REQUEST is missing sequenceId");
    }
    GetResponseBodyResult result;
    result.ok = true;
    result.sequence_id = *fields.sequence_id;
    result.body_json = get_body_json(protocol::ecsp_body(fields.document.get()), result.sequence_id, errcode);
    return result;
}

std::string build_get_response_json(
    const AgentSettings& settings,
    std::string_view request_json,
    const std::string& controller_id,
    const GetResponseBodyResult& body,
    std::uint64_t timestamp_ms
) {
    const auto fields = parse_request(request_json);
    return protocol::build_message_json(
        response_header(settings, protocol::MessageType::GetResponse, fields.header_seq, controller_id, timestamp_ms),
        body.body_json
    );
}

NotifyReplyResult build_notify_reply_body_json(
    std::string_view request_json,
    std::int64_t errcode
) noexcept {
    auto fields = parse_header(request_json);
    if (!fields.ok) {
        return notify_error(fields.error);
    }
    json_object* body = protocol::ecsp_body(fields.document.get());
    json_object* empty = nullptr;
    if (body == nullptr) {
        empty = json_object_new_object();
        body = empty;
    }
    NotifyReplyResult result;
    result.ok = true;
    result.should_reply = !body_has_no_reply(body);
    result.response_type = fields.header_type == protocol::to_underlying(protocol::MessageType::NotifyRequestV2)
        ? protocol::MessageType::NotifyReplyV2
        : protocol::MessageType::NotifyReply;
    if (result.should_reply) {
        result.body_json = notify_body_json(body, errcode);
    }
    if (empty != nullptr) {
        json_object_put(empty);
    }
    return result;
}

std::string build_notify_reply_json(
    const AgentSettings& settings,
    std::string_view request_json,
    const std::string& controller_id,
    const NotifyReplyResult& reply,
    std::uint64_t timestamp_ms
) {
    const auto fields = parse_header(request_json);
    return protocol::build_message_json(
        response_header(settings, reply.response_type, fields.header_seq, controller_id, timestamp_ms),
        reply.body_json
    );
}

PortalAuthReplyResult build_portal_auth_reply_body_json(
    std::string_view request_json,
    std::int64_t errcode
) noexcept {
    auto fields = parse_header(request_json);
    if (!fields.ok) {
        return portal_auth_error(fields.error);
    }
    PortalAuthReplyResult result;
    result.ok = true;
    result.should_reply = fields.header_seq.has_value();
    result.sequence_id = fields.header_seq.has_value()
        ? std::optional<std::int64_t>(*fields.header_seq)
        : std::nullopt;
    result.errcode = errcode;
    result.body_json = "{\"err\":";
    result.body_json += std::to_string(errcode);
    result.body_json += "}";
    return result;
}

std::string build_portal_auth_reply_json(
    const AgentSettings& settings,
    std::string_view request_json,
    const std::string& controller_id,
    const PortalAuthReplyResult& reply,
    std::uint64_t timestamp_ms
) {
    const auto fields = parse_header(request_json);
    return protocol::build_message_json(
        response_header(settings, protocol::MessageType::EventPortalAuthResponse, fields.header_seq, controller_id, timestamp_ms),
        reply.body_json
    );
}

ForgetResponseResult build_forget_response_json(
    const AgentSettings& settings,
    std::string_view request_json,
    const std::string& controller_id,
    std::uint64_t timestamp_ms
) noexcept {
    auto fields = parse_header(request_json);
    ForgetResponseResult result;
    if (!fields.ok) {
        result.error = fields.error;
        return result;
    }
    if (fields.header_type == protocol::to_underlying(protocol::MessageType::ForgetRequestNoReset)) {
        result.response_type = protocol::MessageType::ForgetResponseNoReset;
    } else if (fields.header_type == protocol::to_underlying(protocol::MessageType::ForgetRequest)) {
        result.response_type = protocol::MessageType::ForgetResponse;
    } else {
        result.error = "cannot build forget response for message type";
        return result;
    }
    result.ok = true;
    result.message_json = protocol::build_message_json(
        response_header(settings, result.response_type, std::nullopt, controller_id, timestamp_ms),
        "{}"
    );
    return result;
}

} // namespace openomada::application
