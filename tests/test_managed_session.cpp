#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "openomada/application/inform.hpp"
#include "openomada/application/settings.hpp"
#include "openomada/domain/device_profile.hpp"
#include "openomada/domain/mac_address.hpp"
#include "openomada/lifecycle/managed_session.hpp"
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
    settings.controller_host = "controller.example.test";
    settings.mac = must_parse_mac("02:11:22:33:44:55");
    settings.device_name = "OpenOmada-AP";
    settings.model = "EAP110";
    settings.model_version = "4.0";
    settings.hardware_version = "4.0";
    settings.firmware_version = "5.0.4";
    settings.device_ip = "192.0.2.10";
    settings.controller_id = "0123456789abcdef0123456789abcdef";
    settings.site_id = "0123456789abcdef01234567";
    return settings;
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

void test_inform_scheduler_sends_initial_reply_request_then_periodic_fire_and_forget() {
    openomada::lifecycle::InformScheduler scheduler(3000);
    scheduler.start(5, 1000);

    auto first = scheduler.poll(1000);
    require(first.due, "initial inform is immediately due");
    require(first.need_reply, "initial inform asks for reply");
    require(first.seq == 6, "initial inform seq");
    require(first.uptime_seconds == 0, "initial inform uptime");
    require(first.next_due_ms == 4000, "next due after interval");

    require(!scheduler.poll(3999).due, "inform not due before interval");
    auto second = scheduler.poll(4000);
    require(second.due, "periodic inform due");
    require(!second.need_reply, "periodic inform is fire-and-forget");
    require(second.seq == 7, "periodic inform seq");
    require(second.uptime_seconds == 3, "periodic inform uptime");
}

void test_inform_scheduler_clamps_tiny_intervals() {
    openomada::lifecycle::InformScheduler scheduler(1);
    scheduler.start(0, 0);
    require(scheduler.poll(0).due, "first inform due");
    require(!scheduler.poll(499).due, "clamped interval not elapsed");
    require(scheduler.poll(500).due, "clamped interval elapsed");
}

void test_send_inform_uses_provider_snapshot_and_transport() {
    const auto settings = fixture_settings();
    const openomada::domain::AccessPointProfile profile(settings);
    openomada::application::StaticInformProvider provider({"100.0", 1, "LAN"});
    RecordingTransport transport;

    openomada::lifecycle::ScheduledInform scheduled;
    scheduled.due = true;
    scheduled.need_reply = true;
    scheduled.seq = 8;
    scheduled.uptime_seconds = 42;

    auto status = openomada::lifecycle::send_inform(
        transport,
        settings,
        profile,
        provider,
        settings.controller_id,
        scheduled,
        1780000000000ULL
    );

    require(status.ok, "send inform succeeds");
    require(transport.sent.size() == 1, "one inform sent");

    auto document = openomada::protocol::JsonDocument::parse(transport.sent[0]);
    require(document.valid(), "sent inform parses");
    require(openomada::protocol::ecsp_header_type(document.get()).value_or(0) == openomada::protocol::to_underlying(openomada::protocol::MessageType::InformRequest), "INFORM_REQUEST type");
    require(openomada::protocol::ecsp_header_seq(document.get()).value_or(0) == 8, "INFORM seq");
    auto* body = openomada::protocol::ecsp_body(document.get());
    require(openomada::protocol::json_int(openomada::protocol::object_member(body, "needReply")).value_or(-1) == 1, "needReply set");
    auto* device_info = openomada::protocol::object_member(body, "deviceInfo");
    require(openomada::protocol::json_string(openomada::protocol::object_member(device_info, "upTime")).value_or("") == "42", "uptime from scheduled inform");
}

void test_managed_state_from_adoption_does_not_include_password_material() {
    auto settings = fixture_settings();
    settings.device_username = "lab";
    settings.device_password = "secret-password";
    openomada::lifecycle::AdoptionResult adoption;
    adoption.ok = true;
    adoption.controller_id = settings.controller_id;
    adoption.username = "lab";
    adoption.config_version = 7;
    adoption.sequence_id = 9;
    adoption.last_seq = 5;

    const auto state = openomada::lifecycle::managed_state_from_adoption(
        settings,
        29814,
        adoption,
        1780000000
    );

    require(state.mac == "02:11:22:33:44:55", "managed state MAC normalized");
    require(state.controller_host == "controller.example.test", "managed state controller host");
    require(state.controller_id == settings.controller_id, "managed state controller id");
    require(state.site_id == settings.site_id, "managed state site id");
    require(state.username == "lab", "managed state username");
    require(state.config_version.value_or(0) == 7, "managed state config version");
    require(state.sequence_id.value_or(0) == 9, "managed state sequence id");
}

} // namespace

int main() {
    test_inform_scheduler_sends_initial_reply_request_then_periodic_fire_and_forget();
    test_inform_scheduler_clamps_tiny_intervals();
    test_send_inform_uses_provider_snapshot_and_transport();
    test_managed_state_from_adoption_does_not_include_password_material();
    std::cout << "openomada-managed-session-tests passed\n";
    return 0;
}
