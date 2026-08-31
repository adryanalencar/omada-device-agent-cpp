#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "openomada/application/settings.hpp"
#include "openomada/application/configuration_applier.hpp"
#include "openomada/domain/mac_address.hpp"
#include "openomada/lifecycle/managed_request_handler.hpp"
#include "openomada/lifecycle/session.hpp"
#include "openomada/protocol/json.hpp"
#include "openomada/protocol/message_type.hpp"
#include "openomada/transport/frame_transport.hpp"

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
    settings.mac = must_parse_mac("02:11:22:33:44:55");
    settings.controller_id = "controller-id";
    return settings;
}

openomada::lifecycle::ManagedState managed_state() {
    openomada::lifecycle::ManagedState state;
    state.version = 1;
    state.mac = "02:11:22:33:44:55";
    state.controller_host = "controller.example.test";
    state.controller_id = "controller-id";
    state.manage_port = 29814;
    state.config_version = 2;
    state.sequence_id = 11;
    return state;
}

class RecordingTransport final : public openomada::transport::FrameTransport {
public:
    openomada::transport::TransportStatus send_payload(std::string_view payload) override {
        sent.emplace_back(payload);
        return openomada::transport::TransportStatus::success();
    }

    openomada::transport::ReceiveResult receive_payload() override {
        return {false, true, {}, "timeout"};
    }

    std::vector<std::string> sent;
};

class RecordingApplier final : public openomada::application::ConfigurationApplier {
public:
    openomada::application::ConfigurationApplyResult result{true, true, {}};
    std::size_t calls{0};
    std::size_t wlan_count{0};

    openomada::application::ConfigurationApplyResult apply(
        const openomada::application::AccessPointConfigUpdate& update
    ) override {
        ++calls;
        wlan_count = update.wlans.size();
        return result;
    }
};

std::string request(openomada::protocol::MessageType type, const std::string& body, bool include_seq = true) {
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

std::uint32_t sent_type(const std::string& payload) {
    auto document = openomada::protocol::JsonDocument::parse(payload);
    require(document.valid(), "sent payload parses");
    return openomada::protocol::ecsp_header_type(document.get()).value_or(0);
}

std::int64_t sent_body_int(const std::string& payload, const char* key) {
    auto document = openomada::protocol::JsonDocument::parse(payload);
    require(document.valid(), "sent payload parses");
    return openomada::protocol::json_int(
        openomada::protocol::object_member(openomada::protocol::ecsp_body(document.get()), key)
    ).value_or(-1);
}

void test_passive_set_response_updates_managed_config_version() {
    RecordingTransport transport;
    auto settings = fixture_settings();
    auto state = managed_state();
    const std::string payload = request(
        openomada::protocol::MessageType::SetRequest,
        R"({"sequenceId":14,"configVersionInc":1,"ssh":{"sshenable":"on"}})"
    );

    const auto result = openomada::lifecycle::handle_managed_request(
        transport,
        settings,
        &state,
        payload,
        1780000000000ULL
    );

    require(result.ok, result.error.c_str());
    require(result.action == openomada::lifecycle::ManagedRequestAction::SetResponse, "SET action");
    require(result.response_sent, "SET response sent");
    require(transport.sent.size() == 1, "one response");
    require(sent_type(transport.sent[0]) == openomada::protocol::to_underlying(openomada::protocol::MessageType::SetResponse), "SET_RESPONSE type");
    require(sent_body_int(transport.sent[0], "errcode") == 0, "SET_RESPONSE ok errcode");
    require(sent_body_int(transport.sent[0], "configVersion") == 3, "SET_RESPONSE version");
    require(state.config_version.value_or(0) == 3, "state config version advanced");
    require(state.sequence_id.value_or(0) == 14, "state sequence updated");
}

void test_actionable_set_response_does_not_advance_without_platform_applier() {
    RecordingTransport transport;
    auto settings = fixture_settings();
    auto state = managed_state();
    const std::string payload = request(
        openomada::protocol::MessageType::SetRequest,
        R"({"sequenceId":14,"configVersionInc":1,"ssid_2G":{"ssid":[{"ssidName":"guest"}]}})"
    );

    const auto result = openomada::lifecycle::handle_managed_request(
        transport,
        settings,
        &state,
        payload,
        1780000000000ULL
    );

    require(result.ok, result.error.c_str());
    require(result.response_sent, "SET error response sent");
    require(result.error.find("platform applier") != std::string::npos, "SET actionable error retained");
    require(sent_body_int(transport.sent[0], "errcode") == 1, "SET_RESPONSE error errcode");
    require(sent_body_int(transport.sent[0], "configVersion") == 2, "SET_RESPONSE keeps current version");
    require(state.config_version.value_or(0) == 2, "state config version not advanced");
    require(state.sequence_id.value_or(0) == 11, "state sequence not advanced");
}

void test_actionable_set_response_advances_when_platform_applier_succeeds() {
    RecordingTransport transport;
    RecordingApplier applier;
    auto settings = fixture_settings();
    auto state = managed_state();
    const std::string payload = request(
        openomada::protocol::MessageType::SetRequest,
        R"({"sequenceId":14,"configVersionInc":1,"ssid_2G":{"ssid":[{"ssidName":"guest"}]}})"
    );

    const auto result = openomada::lifecycle::handle_managed_request(
        transport,
        settings,
        &state,
        payload,
        &applier,
        1780000000000ULL
    );

    require(result.ok, result.error.c_str());
    require(applier.calls == 1, "applier called");
    require(applier.wlan_count == 1, "applier receives WLAN update");
    require(result.configuration_applied, "configuration applied flag");
    require(result.configuration_changed, "configuration changed flag");
    require(sent_body_int(transport.sent[0], "errcode") == 0, "SET_RESPONSE ok");
    require(sent_body_int(transport.sent[0], "configVersion") == 3, "version advanced");
    require(state.config_version.value_or(0) == 3, "state config advanced");
    require(state.sequence_id.value_or(0) == 14, "state sequence advanced");
}

void test_actionable_set_response_keeps_version_when_platform_applier_fails() {
    RecordingTransport transport;
    RecordingApplier applier;
    applier.result = {false, false, "uci rejected update"};
    auto settings = fixture_settings();
    auto state = managed_state();
    const std::string payload = request(
        openomada::protocol::MessageType::SetRequest,
        R"({"sequenceId":14,"configVersionInc":1,"ssid_2G":{"ssid":[{"ssidName":"guest"}]}})"
    );

    const auto result = openomada::lifecycle::handle_managed_request(
        transport,
        settings,
        &state,
        payload,
        &applier,
        1780000000000ULL
    );

    require(result.ok, result.error.c_str());
    require(applier.calls == 1, "failing applier called");
    require(result.error == "uci rejected update", "applier error retained");
    require(sent_body_int(transport.sent[0], "errcode") == 1, "SET_RESPONSE error");
    require(sent_body_int(transport.sent[0], "configVersion") == 2, "version kept");
    require(state.config_version.value_or(0) == 2, "state config unchanged");
    require(state.sequence_id.value_or(0) == 11, "state sequence unchanged");
}

void test_get_notify_and_forget_managed_requests() {
    auto settings = fixture_settings();

    {
        RecordingTransport transport;
        auto state = managed_state();
        const auto result = openomada::lifecycle::handle_managed_request(
            transport,
            settings,
            &state,
            request(openomada::protocol::MessageType::GetRequest, R"({"sequenceId":12,"gps":{}})")
        );
        require(result.ok, result.error.c_str());
        require(result.action == openomada::lifecycle::ManagedRequestAction::GetResponse, "GET action");
        require(sent_type(transport.sent[0]) == openomada::protocol::to_underlying(openomada::protocol::MessageType::GetResponse), "GET_RESPONSE type");
        require(sent_body_int(transport.sent[0], "errcode") == 1, "GET_RESPONSE errcode");
    }

    {
        RecordingTransport transport;
        auto state = managed_state();
        const auto result = openomada::lifecycle::handle_managed_request(
            transport,
            settings,
            &state,
            request(openomada::protocol::MessageType::NotifyRequest, R"({"nid":99,"sub":15,"nre":1})")
        );
        require(result.ok, result.error.c_str());
        require(result.action == openomada::lifecycle::ManagedRequestAction::NotifyNoReply, "NOTIFY no-reply action");
        require(transport.sent.empty(), "NOTIFY nre=1 sends no response");
    }

    {
        RecordingTransport transport;
        auto state = managed_state();
        const auto result = openomada::lifecycle::handle_managed_request(
            transport,
            settings,
            &state,
            header_only_request(openomada::protocol::MessageType::ForgetRequestNoReset)
        );
        require(result.ok, result.error.c_str());
        require(result.action == openomada::lifecycle::ManagedRequestAction::ForgetResponse, "FORGET action");
        require(result.should_clear_state, "FORGET clears state");
        require(result.should_end_session, "FORGET ends session");
        require(sent_type(transport.sent[0]) == openomada::protocol::to_underlying(openomada::protocol::MessageType::ForgetResponseNoReset), "FORGET no-reset response type");
    }
}

void test_unknown_managed_message_is_ignored() {
    RecordingTransport transport;
    auto settings = fixture_settings();
    auto state = managed_state();
    const auto result = openomada::lifecycle::handle_managed_request(
        transport,
        settings,
        &state,
        R"({"header":{"type":12345},"body":{}})"
    );

    require(result.ok, result.error.c_str());
    require(result.action == openomada::lifecycle::ManagedRequestAction::Ignored, "unknown ignored");
    require(transport.sent.empty(), "unknown sends no response");
}

} // namespace

int main() {
    test_passive_set_response_updates_managed_config_version();
    test_actionable_set_response_does_not_advance_without_platform_applier();
    test_actionable_set_response_advances_when_platform_applier_succeeds();
    test_actionable_set_response_keeps_version_when_platform_applier_fails();
    test_get_notify_and_forget_managed_requests();
    test_unknown_managed_message_is_ignored();
    std::cout << "openomada-managed-request-tests passed\n";
    return 0;
}
