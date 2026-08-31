#include <cstdlib>
#include <iostream>
#include <string>

#include "openomada/application/configuration_exchange.hpp"
#include "openomada/application/settings.hpp"
#include "openomada/domain/mac_address.hpp"
#include "openomada/protocol/json.hpp"
#include "openomada/protocol/message_type.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

openomada::domain::MacAddress must_parse_mac(const char* value) {
    auto parsed = openomada::domain::MacAddress::parse(value);
    require(parsed.has_value(), "MAC should parse");
    return *parsed;
}

openomada::application::AgentSettings fixture_settings() {
    openomada::application::AgentSettings settings;
    settings.controller_host = "controller.example.test";
    settings.mac = must_parse_mac("02:11:22:33:44:55");
    settings.controller_id = "controller-id";
    return settings;
}

std::string request(
    openomada::protocol::MessageType type,
    const std::string& body,
    bool include_seq = true
) {
    std::string out = R"({"header":{)";
    if (include_seq) {
        out += R"("seq":77,)";
    }
    out += R"("version":"2.3.0","verCap":2,"device":"ap","mac":"02-11-22-33-44-55","type":)";
    out += std::to_string(openomada::protocol::to_underlying(type));
    out += R"(,"error":0},"body":)";
    out += body;
    out += "}";
    return out;
}

std::string header_only_request(openomada::protocol::MessageType type) {
    std::string out = R"({"header":{"seq":99,"version":"2.3.0","verCap":2,"device":"ap","mac":"02-11-22-33-44-55","type":)";
    out += std::to_string(openomada::protocol::to_underlying(type));
    out += R"(,"error":0}})";
    return out;
}

void test_set_response_uses_absolute_config_version() {
    const auto result = openomada::application::build_set_response_body_json(
        request(openomada::protocol::MessageType::SetRequest, R"({"sequenceId":2,"configVersion":1,"led":{"enable":"on"}})"),
        std::nullopt,
        openomada::application::kConfigOk
    );

    require(result.ok, result.error.c_str());
    require(result.body_json == R"({"sequenceId":2,"errcode":0,"configVersion":1})", "absolute SET response body");
}

void test_set_response_derives_incremental_version() {
    const auto result = openomada::application::build_set_response_body_json(
        request(openomada::protocol::MessageType::SetRequest, R"({"sequenceId":14,"configVersionInc":1,"led":{"locate":false}})"),
        2,
        openomada::application::kConfigOk
    );

    require(result.ok, result.error.c_str());
    require(result.config_version == 3, "incremental config version");
    require(result.body_json == R"({"sequenceId":14,"errcode":0,"configVersion":3})", "incremental SET response body");
}

void test_set_response_reports_local_failure_without_advancing_version() {
    const auto result = openomada::application::build_set_response_body_json(
        request(openomada::protocol::MessageType::SetRequest, R"({"sequenceId":14,"configVersionInc":1,"ssid_2G":{"ssid":[{"ssidName":"unsupported"}]}})"),
        2,
        openomada::application::kConfigError
    );

    require(result.ok, result.error.c_str());
    require(result.config_version == 2, "failed SET keeps current version");
    require(result.body_json == R"({"sequenceId":14,"errcode":1,"configVersion":2})", "failed SET response body");
}

void test_set_response_rejects_invalid_version_context() {
    const auto missing_current = openomada::application::build_set_response_body_json(
        request(openomada::protocol::MessageType::SetRequest, R"({"sequenceId":15,"configVersionInc":1})"),
        std::nullopt,
        openomada::application::kConfigOk
    );
    require(!missing_current.ok, "increment without current version rejected");
    require(missing_current.error.find("local config version is unknown") != std::string::npos, "increment missing current error");

    const auto missing_sequence = openomada::application::build_set_response_body_json(
        request(openomada::protocol::MessageType::SetRequest, R"({"configVersion":1})"),
        std::nullopt,
        openomada::application::kConfigOk
    );
    require(!missing_sequence.ok, "missing sequence rejected");
    require(missing_sequence.error.find("sequenceId") != std::string::npos, "missing sequence error");
}

void test_set_response_envelope_preserves_header_sequence() {
    const auto settings = fixture_settings();
    const std::string set_request = request(
        openomada::protocol::MessageType::SetRequest,
        R"({"sequenceId":2,"configVersion":1})"
    );
    const auto body = openomada::application::build_set_response_body_json(
        set_request,
        std::nullopt,
        openomada::application::kConfigOk
    );
    const std::string response = openomada::application::build_set_response_json(
        settings,
        set_request,
        settings.controller_id,
        body,
        1780000000000ULL
    );

    auto document = openomada::protocol::JsonDocument::parse(response);
    require(document.valid(), "SET_RESPONSE parses");
    require(openomada::protocol::ecsp_header_type(document.get()).value_or(0) == openomada::protocol::to_underlying(openomada::protocol::MessageType::SetResponse), "SET_RESPONSE type");
    require(openomada::protocol::ecsp_header_seq(document.get()).value_or(0) == 77, "SET_RESPONSE header seq mirrors request");
    auto* header = openomada::protocol::object_member(document.get(), "header");
    require(openomada::protocol::json_string(openomada::protocol::object_member(header, "dest")).value_or("") == "controller-id", "SET_RESPONSE dest");
}

void test_evaluate_set_request_rejects_unhandled_keys_without_fake_success() {
    const auto evaluation = openomada::application::evaluate_set_request(
        request(openomada::protocol::MessageType::SetRequest, R"({"sequenceId":3,"configVersion":4,"unsupportedCommand":{"enabled":true}})")
    );

    require(evaluation.parsed, "unsupported SET still parses");
    require(evaluation.errcode == openomada::application::kConfigError, "unsupported SET errcode");
    require(evaluation.error.find("unsupportedCommand") != std::string::npos, "unsupported key named");
}

void test_evaluate_set_request_accepts_only_passive_config_without_platform_applier() {
    const auto passive = openomada::application::evaluate_set_request(
        request(openomada::protocol::MessageType::SetRequest, R"({"sequenceId":3,"configVersion":4,"ssh":{"sshenable":"on"}})")
    );
    require(passive.parsed, "passive SET parses");
    require(passive.errcode == openomada::application::kConfigOk, "passive SET can be accepted without applier");

    const auto actionable = openomada::application::evaluate_set_request(
        request(openomada::protocol::MessageType::SetRequest, R"({"sequenceId":3,"configVersion":4,"ssid_2G":{"ssid":[{"ssidName":"guest"}]}})")
    );
    require(actionable.parsed, "actionable SET parses");
    require(actionable.errcode == openomada::application::kConfigError, "actionable SET rejected without applier");
    require(actionable.error.find("platform applier") != std::string::npos, "actionable SET error");
}

void test_get_response_reports_unsupported_keys() {
    const auto body = openomada::application::build_get_response_body_json(
        request(openomada::protocol::MessageType::GetRequest, R"({"sequenceId":12,"powerControl":{},"gps":{}})"),
        openomada::application::kConfigError
    );

    require(body.ok, body.error.c_str());
    require(body.body_json == R"({"sequenceId":12,"errcode":1,"unsupportedKeys":["gps","powerControl"]})", "GET_RESPONSE body");

    const auto settings = fixture_settings();
    const std::string get_request = request(openomada::protocol::MessageType::GetRequest, R"({"sequenceId":12,"gps":{}})");
    const auto get_body = openomada::application::build_get_response_body_json(get_request);
    const std::string response = openomada::application::build_get_response_json(
        settings,
        get_request,
        settings.controller_id,
        get_body,
        1780000000000ULL
    );
    auto document = openomada::protocol::JsonDocument::parse(response);
    require(document.valid(), "GET_RESPONSE parses");
    require(openomada::protocol::ecsp_header_type(document.get()).value_or(0) == openomada::protocol::to_underlying(openomada::protocol::MessageType::GetResponse), "GET_RESPONSE type");
    require(openomada::protocol::ecsp_header_seq(document.get()).value_or(0) == 77, "GET_RESPONSE header seq");
}

void test_notify_reply_and_no_reply_flag() {
    const auto reply = openomada::application::build_notify_reply_body_json(
        request(openomada::protocol::MessageType::NotifyRequestV2, R"({"nid":99,"sub":15,"ctnt":{"value":true}})")
    );

    require(reply.ok, reply.error.c_str());
    require(reply.should_reply, "notify reply required");
    require(reply.response_type == openomada::protocol::MessageType::NotifyReplyV2, "V2 notify response type");
    require(reply.body_json == R"({"err":1,"nid":99,"sub":15,"rst":{"error":"unsupported notify request"}})", "notify reply body");

    const auto no_reply = openomada::application::build_notify_reply_body_json(
        request(openomada::protocol::MessageType::NotifyRequest, R"({"nid":99,"sub":15,"nre":1})")
    );
    require(no_reply.ok, no_reply.error.c_str());
    require(!no_reply.should_reply, "nre=1 suppresses notify reply");
    require(no_reply.body_json.empty(), "no reply has no body");
}

void test_forget_response_uses_confirmed_response_type_without_seq() {
    const auto settings = fixture_settings();
    const auto response = openomada::application::build_forget_response_json(
        settings,
        header_only_request(openomada::protocol::MessageType::ForgetRequest),
        settings.controller_id,
        1780000000000ULL
    );

    require(response.ok, response.error.c_str());
    require(response.response_type == openomada::protocol::MessageType::ForgetResponse, "FORGET response type");
    auto document = openomada::protocol::JsonDocument::parse(response.message_json);
    require(document.valid(), "FORGET_RESPONSE parses");
    require(openomada::protocol::ecsp_header_type(document.get()).value_or(0) == openomada::protocol::to_underlying(openomada::protocol::MessageType::ForgetResponse), "FORGET_RESPONSE type");
    require(!openomada::protocol::ecsp_header_seq(document.get()).has_value(), "FORGET_RESPONSE omits header seq");

    const auto no_reset = openomada::application::build_forget_response_json(
        settings,
        header_only_request(openomada::protocol::MessageType::ForgetRequestNoReset),
        settings.controller_id,
        1780000000000ULL
    );
    require(no_reset.ok, no_reset.error.c_str());
    require(no_reset.response_type == openomada::protocol::MessageType::ForgetResponseNoReset, "FORGET no-reset response type");
}

} // namespace

int main() {
    test_set_response_uses_absolute_config_version();
    test_set_response_derives_incremental_version();
    test_set_response_reports_local_failure_without_advancing_version();
    test_set_response_rejects_invalid_version_context();
    test_set_response_envelope_preserves_header_sequence();
    test_evaluate_set_request_rejects_unhandled_keys_without_fake_success();
    test_evaluate_set_request_accepts_only_passive_config_without_platform_applier();
    test_get_response_reports_unsupported_keys();
    test_notify_reply_and_no_reply_flag();
    test_forget_response_uses_confirmed_response_type_without_seq();
    std::cout << "openomada-configuration-exchange-tests passed\n";
    return 0;
}
