#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "openomada/crypto/ecsp_auth.hpp"
#include "openomada/crypto/hash.hpp"
#include "openomada/application/settings.hpp"
#include "openomada/domain/device_profile.hpp"
#include "openomada/domain/mac_address.hpp"
#include "openomada/lifecycle/session.hpp"
#include "openomada/protocol/ecsp_builders.hpp"
#include "openomada/protocol/ecsp_message.hpp"
#include "openomada/protocol/frame_codec.hpp"
#include "openomada/protocol/json.hpp"
#include "openomada/protocol/message_type.hpp"

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

void test_mac_policy() {
    const auto mac = must_parse_mac("AA-BB-CC-DD-EE-FF");
    require(mac.normalized() == "aa:bb:cc:dd:ee:ff", "MAC internal form");
    require(mac.omada() == "AA-BB-CC-DD-EE-FF", "MAC Omada form");
    require(must_parse_mac("aabb.ccdd.eeff").normalized() == "aa:bb:cc:dd:ee:ff", "MAC separator tolerance");
    require(!openomada::domain::MacAddress::parse("aa:bb:cc").has_value(), "short MAC rejected");
    require(!openomada::domain::MacAddress::parse("aa:bb:cc:dd:ee:ff:00").has_value(), "long MAC rejected");
}

void test_message_types() {
    using openomada::protocol::MessageType;
    using openomada::protocol::to_underlying;

    require(to_underlying(MessageType::Discovery) == 1, "DISCOVERY id");
    require(to_underlying(MessageType::EventPortalQuery) == 64, "EVENT_PORTAL_QUERY id");
    require(to_underlying(MessageType::EventPortalAuth) == 128, "EVENT_PORTAL_AUTH id");
    require(to_underlying(MessageType::EventPortalAuthResponse) == 352, "EVENT_PORTAL_AUTH_RESPONSE id");
    require(to_underlying(MessageType::InformRequest) == 256, "INFORM_REQUEST id");
    require(to_underlying(MessageType::GetRequest) == 24576, "GET_REQUEST id");
    require(to_underlying(MessageType::GetResponse) == 28672, "GET_RESPONSE id");
    require(to_underlying(MessageType::DeviceVerifyInfo) == 0x100001, "DEVICE_VERIFY_INFO id");
    require(to_underlying(MessageType::SystemVerifyResult) == 0x100003, "SYSTEM_VERIFY_RESULT id");
    require(to_underlying(MessageType::Report) == 0x150000, "REPORT id");
}

void test_message_builder_and_frame_codec() {
    using openomada::protocol::DecodeResult;
    using openomada::protocol::EcspHeader;
    using openomada::protocol::FrameError;
    using openomada::protocol::MessageType;
    using openomada::protocol::build_message_json;
    using openomada::protocol::decode_frame;
    using openomada::protocol::encode_frame;

    EcspHeader header{
        1,
        "2.3.0",
        2,
        "ap",
        must_parse_mac("02:11:22:33:44:55"),
        MessageType::Discovery,
        0,
        "",
        std::nullopt,
    };
    const std::string payload = build_message_json(
        header,
        R"({"deviceInfo":{"isFactory":true}})"
    );
    const std::string expected =
        R"({"header":{"seq":1,"version":"2.3.0","verCap":2,"device":"ap","mac":"02-11-22-33-44-55","type":1,"error":0},"body":{"deviceInfo":{"isFactory":true}}})";
    require(payload == expected, "ECSP JSON header serialization matches Python fixture");

    const std::vector<std::uint8_t> frame = encode_frame(payload);
    require(frame.size() == payload.size() + openomada::protocol::kLengthSize, "frame size");
    require(frame[0] == 0 && frame[1] == 0 && frame[2] == 0, "frame big-endian high bytes");
    require(frame[3] == static_cast<std::uint8_t>(payload.size()), "frame big-endian length low byte");

    DecodeResult decoded = decode_frame(frame);
    require(decoded.ok(), "frame decodes");
    require(decoded.declared_length == payload.size(), "declared length");
    require(decoded.payload == payload, "decoded payload");

    std::vector<std::uint8_t> mismatch = frame;
    mismatch[3] = static_cast<std::uint8_t>(mismatch[3] + 1U);
    require(decode_frame(mismatch).error == FrameError::LengthMismatch, "length mismatch rejected");
    const std::vector<std::uint8_t> too_short{0, 0, 0};
    require(decode_frame(too_short).error == FrameError::TooShort, "short frame rejected");
}

void test_auth_fixtures() {
    using openomada::crypto::calculate_md5_mode_auth;
    using openomada::crypto::upper_md5;
    using openomada::crypto::upper_sha256;

    require(upper_md5("test-password") == "DFB450EFDDBB5387197C84460623675B", "uppercase MD5");
    require(
        upper_sha256("labDFB450EFDDBB5387197C84460623675B") ==
            "7D7EE68FDD24EBFA10A9B2188368CE539BB4C5A872E02ED409F6E1C21386D7FB",
        "uppercase SHA256"
    );
    require(
        calculate_md5_mode_auth(
            "lab",
            "test-password",
            "12345678-1234-1234-1234-123456789abc"
        ) == "5D8305A898E620B9A6DC70A597206AFBBFB1DDFA42F7A5550DA477EECFB2CB73",
        "ECSP cipherType=5 auth proof"
    );
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
    settings.device_username = "lab";
    settings.device_password = "test-password";
    return settings;
}

void test_lifecycle_transitions() {
    using openomada::lifecycle::ControllerSession;
    using openomada::lifecycle::LifecycleState;
    using openomada::lifecycle::can_transition;
    using openomada::lifecycle::to_string;

    ControllerSession session;
    require(to_string(session.state) == std::string("disconnected"), "initial lifecycle state");
    require(session.transition(LifecycleState::Discovering), "disconnected -> discovering");
    require(session.transition(LifecycleState::Adopting), "discovering -> adopting");
    require(session.transition(LifecycleState::Verifying), "adopting -> verifying");
    require(session.transition(LifecycleState::Negotiating), "verifying -> negotiating");
    require(session.transition(LifecycleState::Managed), "negotiating -> managed");
    require(!can_transition(LifecycleState::Managed, LifecycleState::Adopting), "invalid transition rejected");
}

void test_json_boundary_helpers() {
    using openomada::protocol::JsonDocument;
    using openomada::protocol::ecsp_body;
    using openomada::protocol::ecsp_header_error;
    using openomada::protocol::ecsp_header_seq;
    using openomada::protocol::ecsp_header_type;
    using openomada::protocol::json_string;
    using openomada::protocol::object_member;

    auto doc = JsonDocument::parse(R"({"header":{"type":1048576,"seq":7},"body":{"username":"lab"}})");
    require(doc.valid(), "JSON document parses");
    require(ecsp_header_type(doc.get()).value_or(0) == 1048576, "header type parsed");
    require(ecsp_header_seq(doc.get()).value_or(0) == 7, "header seq parsed");
    require(ecsp_header_error(doc.get()) == 0, "missing error defaults to zero");
    require(json_string(object_member(ecsp_body(doc.get()), "username")).value_or("") == "lab", "body string parsed");
    require(!JsonDocument::parse("[1,2,3]").valid(), "non-object JSON rejected");
}

void test_phase2_message_builders() {
    using openomada::domain::AccessPointProfile;
    using openomada::protocol::build_device_negotiation_json;
    using openomada::protocol::build_discovery_json;
    using openomada::protocol::build_init_sync_result_json;
    using openomada::protocol::build_preconnect_json;

    const auto settings = fixture_settings();
    const AccessPointProfile profile(settings);

    const std::string discovery = build_discovery_json(settings, profile, 1, false, 1234);
    require(discovery.find(R"("type":1)") != std::string::npos, "discovery type");
    require(discovery.find(R"("dest":"0123456789abcdef01234567")") != std::string::npos, "site-scoped discovery dest");
    require(discovery.find(R"("controllerId":"0123456789abcdef0123456789abcdef")") != std::string::npos, "controller id in discovery");
    require(discovery.find(R"("destOmadacId":"0123456789abcdef01234567")") != std::string::npos, "site id in controllerSetting");
    require(discovery.find(R"("isFactory":true)") != std::string::npos, "factory discovery");
    require(discovery.find(R"("mainMac":"02-11-22-33-44-55")") != std::string::npos, "Omada mainMac");

    const std::string rediscovery = build_discovery_json(settings, profile, 1, true, 1234);
    require(rediscovery.find(R"("isFactory":false)") != std::string::npos, "managed rediscovery is not factory");

    const std::string preconnect = build_preconnect_json(settings, profile, 2, settings.controller_id, true, 1234);
    require(preconnect.find(R"("type":3)") != std::string::npos, "preconnect type");
    require(preconnect.find(R"("rebuild":1)") != std::string::npos, "managed reconnect rebuild shape");
    require(preconnect.find(R"("isFactory":false)") != std::string::npos, "managed preconnect not factory");

    const std::string negotiation = build_device_negotiation_json(settings, profile, 4, settings.controller_id, 7, 1234);
    require(negotiation.find(R"("type":1048580)") != std::string::npos, "device negotiation type");
    require(negotiation.find(R"("configVersion":7)") != std::string::npos, "persisted config version");
    require(negotiation.find(R"("components_v2":{"system":"2.0")") != std::string::npos, "conservative components");
    require(negotiation.find(R"("ip":)") == std::string::npos, "adoption deviceInfo omits ip");

    const std::string init_sync = build_init_sync_result_json(settings, 5, settings.controller_id, 1234);
    require(init_sync.find(R"("type":1048582)") != std::string::npos, "init sync type");
    require(init_sync.find(R"("body")") == std::string::npos, "init sync omits body");
}

} // namespace

int main() {
    test_mac_policy();
    test_message_types();
    test_message_builder_and_frame_codec();
    test_auth_fixtures();
    test_lifecycle_transitions();
    test_json_boundary_helpers();
    test_phase2_message_builders();
    std::cout << "openomada-core-tests passed\n";
    return 0;
}
