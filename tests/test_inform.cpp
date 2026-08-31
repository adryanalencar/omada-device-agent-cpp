#include <cstdlib>
#include <iostream>
#include <string>

#include "openomada/application/inform.hpp"
#include "openomada/application/settings.hpp"
#include "openomada/domain/device_profile.hpp"
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
    settings.device_name = "OpenOmada-AP";
    settings.model = "EAP110";
    settings.model_version = "4.0";
    settings.hardware_version = "4.0";
    settings.firmware_version = "5.0.4";
    settings.device_ip = "192.0.2.10";
    settings.controller_id = "0123456789abcdef0123456789abcdef";
    return settings;
}

void test_inform_body_matches_minimal_python_projection() {
    const auto settings = fixture_settings();
    const openomada::domain::AccessPointProfile profile(settings);
    openomada::application::StaticInformProvider provider({
        "100.0",
        1,
        "LAN",
    });

    const auto snapshot = provider.build(true, 12);
    const std::string body = openomada::application::build_inform_body_json(profile, snapshot);

    auto document = openomada::protocol::JsonDocument::parse(body);
    require(document.valid(), "inform body parses");
    auto* root = document.get();
    require(openomada::protocol::json_int(openomada::protocol::object_member(root, "needReply")).value_or(-1) == 1, "needReply true");

    auto* device_info = openomada::protocol::object_member(root, "deviceInfo");
    require(openomada::protocol::json_string(openomada::protocol::object_member(device_info, "upTime")).value_or("") == "12", "uptime is string");
    require(json_object_get_boolean(openomada::protocol::object_member(device_info, "isFactory")) == 0, "inform is not factory");
    require(openomada::protocol::json_string(openomada::protocol::object_member(device_info, "mainMac")).value_or("") == "02-11-22-33-44-55", "mainMac Omada format");

    auto* lan = openomada::protocol::object_member(root, "lanInfo");
    require(openomada::protocol::json_string(openomada::protocol::object_member(lan, "rate")).value_or("") == "100.0", "LAN rate string");
    require(openomada::protocol::json_int(openomada::protocol::object_member(lan, "duplex")).value_or(-1) == 1, "LAN duplex");
    require(openomada::protocol::json_string(openomada::protocol::object_member(lan, "port")).value_or("") == "LAN", "LAN port");
}

void test_inform_request_uses_ecsp_envelope() {
    const auto settings = fixture_settings();
    const openomada::domain::AccessPointProfile profile(settings);
    openomada::application::InformSnapshot snapshot;
    snapshot.need_reply = false;
    snapshot.uptime_seconds = 5;

    const std::string message = openomada::application::build_inform_request_json(
        settings,
        profile,
        6,
        settings.controller_id,
        snapshot,
        1780000000000ULL
    );

    auto document = openomada::protocol::JsonDocument::parse(message);
    require(document.valid(), "inform message parses");
    require(openomada::protocol::ecsp_header_type(document.get()).value_or(0) == openomada::protocol::to_underlying(openomada::protocol::MessageType::InformRequest), "INFORM_REQUEST type");
    require(openomada::protocol::ecsp_header_seq(document.get()).value_or(0) == 6, "inform seq");

    auto* header = openomada::protocol::object_member(document.get(), "header");
    require(openomada::protocol::json_string(openomada::protocol::object_member(header, "dest")).value_or("") == settings.controller_id, "inform dest");
    require(openomada::protocol::json_string(openomada::protocol::object_member(header, "mac")).value_or("") == "02-11-22-33-44-55", "header MAC Omada format");
}

} // namespace

int main() {
    test_inform_body_matches_minimal_python_projection();
    test_inform_request_uses_ecsp_envelope();
    std::cout << "openomada-inform-tests passed\n";
    return 0;
}
