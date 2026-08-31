#include <cstdlib>
#include <deque>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "openomada/application/settings.hpp"
#include "openomada/crypto/ecsp_auth.hpp"
#include "openomada/domain/device_profile.hpp"
#include "openomada/domain/mac_address.hpp"
#include "openomada/lifecycle/adoption.hpp"
#include "openomada/lifecycle/discovery.hpp"
#include "openomada/protocol/ecsp_message.hpp"
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

openomada::domain::MacAddress must_parse_mac(std::string_view value) {
    auto parsed = openomada::domain::MacAddress::parse(value);
    require(parsed.has_value(), "MAC should parse");
    return *parsed;
}

openomada::application::AgentSettings fixture_settings() {
    openomada::application::AgentSettings settings;
    settings.controller_host = "omada.example.test";
    settings.mac = must_parse_mac("02:11:22:33:44:55");
    settings.device_name = "OpenOmada-AP";
    settings.model = "EAP110";
    settings.model_version = "4.0";
    settings.hardware_version = "4.0";
    settings.firmware_version = "5.0.4";
    settings.device_ip = "192.0.2.10";
    settings.controller_id = "0123456789abcdef0123456789abcdef";
    settings.site_id = "0123456789abcdef01234567";
    settings.device_password = "test-password";
    return settings;
}

std::string controller_message(
    openomada::protocol::MessageType type,
    std::string_view body_json,
    int error = 0,
    bool include_body = true
) {
    openomada::protocol::EcspHeader header{
        1,
        "2.3.0",
        2,
        "ap",
        must_parse_mac("02:11:22:33:44:55"),
        type,
        error,
        "0123456789abcdef0123456789abcdef",
        1234,
    };
    return openomada::protocol::build_message_json(header, body_json, include_body);
}

std::uint32_t message_type(const std::string& payload) {
    auto document = openomada::protocol::JsonDocument::parse(payload);
    require(document.valid(), "sent message must be JSON object");
    return openomada::protocol::ecsp_header_type(document.get()).value_or(0);
}

std::uint32_t message_seq(const std::string& payload) {
    auto document = openomada::protocol::JsonDocument::parse(payload);
    require(document.valid(), "sent message must be JSON object");
    return openomada::protocol::ecsp_header_seq(document.get()).value_or(0);
}

class ScriptedTransport final : public openomada::transport::FrameTransport {
public:
    explicit ScriptedTransport(std::deque<std::string> inbound) : inbound_(std::move(inbound)) {}

    openomada::transport::TransportStatus send_payload(std::string_view payload) override {
        sent.emplace_back(payload);
        return openomada::transport::TransportStatus::success();
    }

    openomada::transport::ReceiveResult receive_payload() override {
        if (inbound_.empty()) {
            return {false, true, {}, "timeout"};
        }
        std::string payload = std::move(inbound_.front());
        inbound_.pop_front();
        return {true, false, std::move(payload), {}};
    }

    std::vector<std::string> sent;

private:
    std::deque<std::string> inbound_;
};

void test_pre_adopt_parser_honors_adopt_port_and_dest() {
    const std::string payload = controller_message(
        openomada::protocol::MessageType::PreAdoptRequest,
        R"({"adoptPort":29814})"
    );

    const auto parsed = openomada::lifecycle::parse_pre_adopt_request(payload, "", 29814);

    require(parsed.ok, "PRE_ADOPT parser succeeds");
    require(parsed.is_pre_adopt, "PRE_ADOPT type recognized");
    require(parsed.adopt_port == 29814, "adoptPort parsed");
    require(parsed.controller_id == "0123456789abcdef0123456789abcdef", "controller dest learned");
}

void test_pre_adopt_parser_keeps_site_dest_separate() {
    openomada::protocol::EcspHeader header{
        9,
        "2.3.0",
        2,
        "ap",
        must_parse_mac("02:11:22:33:44:55"),
        openomada::protocol::MessageType::PreAdoptRequest,
        0,
        "0123456789abcdef01234567",
        1234,
    };
    const std::string payload = openomada::protocol::build_message_json(header, R"({})");

    const auto parsed = openomada::lifecycle::parse_pre_adopt_request(
        payload,
        "0123456789abcdef0123456789abcdef",
        29814
    );

    require(parsed.ok, "site PRE_ADOPT parser succeeds");
    require(parsed.controller_id == "0123456789abcdef0123456789abcdef", "logical controller id preserved");
    require(parsed.destination_id == "0123456789abcdef01234567", "site destination separated");
}

void test_successful_v2_initial_sync_sequence() {
    using openomada::protocol::MessageType;
    using openomada::protocol::to_underlying;

    const auto settings = fixture_settings();
    const openomada::domain::AccessPointProfile profile(settings);
    const std::string device_random_key = "12345678-1234-1234-1234-123456789abc";
    const std::string system_random_key = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    const std::string controller_auth = openomada::crypto::calculate_md5_mode_auth(
        "lab",
        settings.device_password,
        system_random_key
    );

    ScriptedTransport transport({
        controller_message(
            MessageType::PreConnectInfoResponse,
            R"({"username":"lab","randomKeyForDeviceVerify":")" + device_random_key + R"("})"
        ),
        controller_message(
            MessageType::DeviceVerifyResponse,
            R"({"auth":")" + controller_auth + R"("})"
        ),
        controller_message(MessageType::VerifyResultAck, R"({})"),
        controller_message(MessageType::SystemNegotiation, R"({"configVersion":7,"sequenceId":9})"),
        controller_message(MessageType::InitSyncResultAck, R"({})"),
    });

    openomada::lifecycle::AdoptionOptions options;
    options.timestamp_ms = 1780000000000ULL;
    options.random_system_key = [system_random_key]() { return system_random_key; };

    const auto result = openomada::lifecycle::run_v2_initial_sync(
        transport,
        settings,
        profile,
        settings.controller_id,
        options
    );

    require(result.ok, "initial sync succeeds");
    require(result.reached_system_verify, "system verify reached");
    require(result.initial_sync_complete, "initial sync complete");
    require(result.username == "lab", "username learned");
    require(result.config_version.value_or(0) == 7, "configVersion captured");
    require(result.sequence_id.value_or(0) == 9, "sequenceId captured");
    require(result.last_seq == 5, "last ECSP seq captured");
    require(transport.sent.size() == 5, "five handshake messages sent");
    require(message_type(transport.sent[0]) == to_underlying(MessageType::PreConnectInfo), "sent PRE_CONNECT_INFO");
    require(message_type(transport.sent[1]) == to_underlying(MessageType::DeviceVerifyInfo), "sent DEVICE_VERIFY_INFO");
    require(message_type(transport.sent[2]) == to_underlying(MessageType::SystemVerifyResult), "sent SYSTEM_VERIFY_RESULT");
    require(message_type(transport.sent[3]) == to_underlying(MessageType::DeviceNegotiation), "sent DEVICE_NEGOTIATION");
    require(message_type(transport.sent[4]) == to_underlying(MessageType::InitSyncResult), "sent INIT_SYNC_RESULT");
    require(message_seq(transport.sent[0]) == 1, "seq 1");
    require(message_seq(transport.sent[4]) == 5, "seq 5");

    const std::string expected_device_auth = openomada::crypto::calculate_md5_mode_auth(
        "lab",
        settings.device_password,
        device_random_key
    );
    require(transport.sent[1].find(expected_device_auth) != std::string::npos, "device auth proof sent");
    require(transport.sent[1].find(R"("cipherType":5)") != std::string::npos, "cipher type 5 sent");
    require(transport.sent[1].find(system_random_key) != std::string::npos, "system random key sent");
    require(transport.sent[3].find(R"("components_v2":{"system":"2.0")") != std::string::npos, "conservative components sent");
    require(transport.sent[3].find(R"("isFactory")") == std::string::npos, "negotiation deviceInfo omits isFactory");
    require(transport.sent[3].find(R"("ip")") == std::string::npos, "negotiation deviceInfo omits ip");
}

void test_v2_initial_sync_rejects_controller_auth_mismatch() {
    using openomada::protocol::MessageType;

    const auto settings = fixture_settings();
    const openomada::domain::AccessPointProfile profile(settings);

    ScriptedTransport transport({
        controller_message(
            MessageType::PreConnectInfoResponse,
            R"({"username":"lab","randomKeyForDeviceVerify":"12345678-1234-1234-1234-123456789abc"})"
        ),
        controller_message(MessageType::DeviceVerifyResponse, R"({"auth":"bad"})"),
    });

    openomada::lifecycle::AdoptionOptions options;
    options.random_system_key = []() { return "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"; };

    const auto result = openomada::lifecycle::run_v2_initial_sync(
        transport,
        settings,
        profile,
        settings.controller_id,
        options
    );

    require(!result.ok, "auth mismatch fails");
    require(result.error == openomada::lifecycle::AdoptionError::ControllerAuthMismatch, "auth mismatch error");
    require(transport.sent.size() == 2, "stops after DEVICE_VERIFY_RESPONSE mismatch");
}

void test_v2_initial_sync_requires_local_password() {
    using openomada::protocol::MessageType;

    auto settings = fixture_settings();
    settings.device_password.clear();
    const openomada::domain::AccessPointProfile profile(settings);

    ScriptedTransport transport({
        controller_message(
            MessageType::PreConnectInfoResponse,
            R"({"username":"lab","randomKeyForDeviceVerify":"12345678-1234-1234-1234-123456789abc"})"
        ),
    });

    const auto result = openomada::lifecycle::run_v2_initial_sync(
        transport,
        settings,
        profile,
        settings.controller_id
    );

    require(!result.ok, "missing password fails");
    require(result.error == openomada::lifecycle::AdoptionError::MissingPassword, "missing password error");
    require(transport.sent.size() == 1, "only PRE_CONNECT_INFO sent without password");
}

} // namespace

int main() {
    test_pre_adopt_parser_honors_adopt_port_and_dest();
    test_pre_adopt_parser_keeps_site_dest_separate();
    test_successful_v2_initial_sync_sequence();
    test_v2_initial_sync_rejects_controller_auth_mismatch();
    test_v2_initial_sync_requires_local_password();
    std::cout << "openomada-lifecycle-tests passed\n";
    return 0;
}
