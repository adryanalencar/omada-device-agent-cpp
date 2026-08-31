#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "openomada/application/inform.hpp"
#include "openomada/domain/device_profile.hpp"
#include "openomada/domain/mac_address.hpp"
#include "openomada/openwrt/telemetry.hpp"
#include "openomada/protocol/json.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string member_json(const std::vector<openomada::application::JsonBodyMember>& members, const std::string& key) {
    for (const auto& member : members) {
        if (member.key == key) {
            return member.json;
        }
    }
    return {};
}

openomada::domain::MacAddress mac(const char* value) {
    auto parsed = openomada::domain::MacAddress::parse(value);
    require(parsed.has_value(), "MAC parses");
    return *parsed;
}

void test_maps_openwrt_wireless_status_to_omada_inform_members() {
    const auto result = openomada::openwrt::openwrt_wireless_inform_from_status_json(R"({
        "radio0": {
            "config": {"channel": "11", "htmode": "HT20", "hwmode": "11g", "txpower": 17},
            "interfaces": [{
                "config": {"ssid": "guest"},
                "bssid": "02:00:00:00:00:10",
                "stations": {"aa:bb:cc:dd:ee:ff": {}},
                "statistics": {
                    "tx_bytes": "1234",
                    "rx_bytes": "567",
                    "tx_packets": 12,
                    "rx_packets": 7
                }
            }]
        },
        "radio1": {
            "config": {"channel": 36, "htmode": "VHT80"},
            "interfaces": [{"config": {"ssid": "corp"}, "num_sta": 2}]
        }
    })");

    require(result.ok, result.error.c_str());
    auto w2g = openomada::protocol::JsonDocument::parse(member_json(result.members, "wSettings_2G"));
    require(w2g.valid(), "2g settings parse");
    require(openomada::protocol::json_string(openomada::protocol::object_member(w2g.get(), "ch")).value_or("") == "11", "2g channel");
    require(openomada::protocol::json_string(openomada::protocol::object_member(w2g.get(), "bw")).value_or("") == "HT20", "2g bw");
    require(openomada::protocol::json_string(openomada::protocol::object_member(w2g.get(), "rdMode")).value_or("") == "11g", "2g mode");
    require(openomada::protocol::json_string(openomada::protocol::object_member(w2g.get(), "txPower")).value_or("") == "17", "2g tx power");
    require(openomada::protocol::json_int(openomada::protocol::object_member(w2g.get(), "staNum")).value_or(-1) == 1, "2g station count");

    auto ssid2g = openomada::protocol::JsonDocument::parse("{\"items\":" + member_json(result.members, "ssidStats_2G") + "}");
    require(ssid2g.valid(), "2g ssid stats parse");
    auto* item = json_object_array_get_idx(openomada::protocol::object_member(ssid2g.get(), "items"), 0);
    require(openomada::protocol::json_string(openomada::protocol::object_member(item, "ssid")).value_or("") == "guest", "ssid");
    require(openomada::protocol::json_string(openomada::protocol::object_member(item, "bssid")).value_or("") == "02-00-00-00-00-10", "bssid omada");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "down")).value_or(-1) == 1234, "down");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "up")).value_or(-1) == 567, "up");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "downPkts")).value_or(-1) == 12, "down packets");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "upPkts")).value_or(-1) == 7, "up packets");

    auto w5g = openomada::protocol::JsonDocument::parse(member_json(result.members, "wSettings_5G"));
    require(w5g.valid(), "5g settings parse");
    require(openomada::protocol::json_string(openomada::protocol::object_member(w5g.get(), "ch")).value_or("") == "36", "5g channel");
    require(openomada::protocol::json_string(openomada::protocol::object_member(w5g.get(), "bw")).value_or("") == "VHT80", "5g bw");
    require(openomada::protocol::json_int(openomada::protocol::object_member(w5g.get(), "staNum")).value_or(-1) == 2, "5g stations");
}

void test_extracts_hostapd_interfaces_from_wireless_status() {
    const auto result = openomada::openwrt::openwrt_wireless_interfaces_from_status_json(R"({
        "radio0": {
            "interfaces": [
                {"ifname": "wlan0", "config": {"ssid": "guest"}},
                {"section": "wlan0-1", "config": {"ssid": "iot"}}
            ]
        }
    })");

    require(result.ok, result.error.c_str());
    require(result.interfaces.size() == 2, "two interfaces");
    require(result.interfaces[0].ifname == "wlan0", "ifname");
    require(result.interfaces[0].ssid == "guest", "ssid");
    require(result.interfaces[0].band.value_or(openomada::application::RadioBand::SixG) == openomada::application::RadioBand::TwoG, "band");
    require(result.interfaces[1].ifname == "wlan0-1", "section fallback");
}

void test_maps_hostapd_clients_to_wireless_client_state() {
    openomada::openwrt::OpenWrtWirelessInterface interface;
    interface.ifname = "wlan0";
    interface.ssid = "guest";
    interface.band = openomada::application::RadioBand::TwoG;

    const auto result = openomada::openwrt::hostapd_client_states_from_json(interface, R"({
        "clients": {
            "AA-BB-CC-DD-EE-FF": {
                "signal": -61,
                "snr": 35,
                "bytes": {"tx": 1200, "rx": 345},
                "packets": {"tx": 12, "rx": 5},
                "rate": {"tx": 7200, "rx": 6500},
                "connected_time": 42
            }
        }
    })");

    require(result.ok, result.error.c_str());
    require(result.clients.size() == 1, "one client");
    const auto& client = result.clients[0];
    require(client.mac.normalized() == "aa:bb:cc:dd:ee:ff", "client MAC normalized");
    require(client.ssid == "guest", "client ssid");
    require(client.radio.value_or(openomada::application::RadioBand::SixG) == openomada::application::RadioBand::TwoG, "client band");
    require(client.rssi.value_or(0) == -61, "rssi");
    require(client.snr.value_or(0) == 35, "snr");
    require(client.rx_bytes == 1200, "rx bytes as downlink");
    require(client.tx_bytes == 345, "tx bytes as uplink");
    require(client.tx_packets.value_or(0) == 12, "tx packets");
    require(client.rx_packets.value_or(0) == 5, "rx packets");
    require(client.tx_rate.value_or(0) == 7200, "tx rate");
    require(client.rx_rate.value_or(0) == 6500, "rx rate");
    require(client.association_time.value_or(0) == 42, "association time");
}

void test_hostapd_augments_ssid_counts_and_radio_traffic() {
    const std::string status = R"({
        "radio0": {
            "config": {"channel": 6, "htmode": "HT20"},
            "interfaces": [{"ifname": "phy0-ap0", "config": {"ssid": "guest"}, "stations": []}]
        }
    })";
    const std::vector<std::string> hostapd = {
        R"({"clients":{"aa:bb:cc:dd:ee:ff":{"bytes":{"rx":100,"tx":200},"packets":{"rx":1,"tx":2}}}})",
    };

    const auto result = openomada::openwrt::openwrt_wireless_inform_from_status_and_hostapd_json(status, hostapd);

    require(result.ok, result.error.c_str());
    auto settings = openomada::protocol::JsonDocument::parse(member_json(result.members, "wSettings_2G"));
    require(settings.valid(), "settings parse");
    require(openomada::protocol::json_int(openomada::protocol::object_member(settings.get(), "staNum")).value_or(-1) == 1, "hostapd station count");

    auto traffic = openomada::protocol::JsonDocument::parse(member_json(result.members, "radioTraffic_2G"));
    require(traffic.valid(), "traffic parse");
    require(openomada::protocol::json_int(openomada::protocol::object_member(traffic.get(), "tx")).value_or(-1) == 200, "traffic tx");
    require(openomada::protocol::json_int(openomada::protocol::object_member(traffic.get(), "rx")).value_or(-1) == 100, "traffic rx");
    require(openomada::protocol::json_int(openomada::protocol::object_member(traffic.get(), "txP")).value_or(-1) == 2, "traffic txP");
    require(openomada::protocol::json_int(openomada::protocol::object_member(traffic.get(), "rxP")).value_or(-1) == 1, "traffic rxP");
    require(openomada::protocol::json_int(openomada::protocol::object_member(traffic.get(), "txDP")).value_or(-1) == 0, "traffic txDP default");

    auto ssids = openomada::protocol::JsonDocument::parse("{\"items\":" + member_json(result.members, "ssidStats_2G") + "}");
    require(ssids.valid(), "ssid stats parse");
    auto* item = json_object_array_get_idx(openomada::protocol::object_member(ssids.get(), "items"), 0);
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "clntNum")).value_or(-1) == 1, "SSID client count");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "down")).value_or(-1) == 200, "SSID down");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "up")).value_or(-1) == 100, "SSID up");
}

void test_inform_body_accepts_openwrt_extra_members_and_clients() {
    openomada::application::AgentSettings settings;
    settings.mac = mac("02:11:22:33:44:55");
    settings.device_ip = "192.0.2.10";
    const openomada::domain::AccessPointProfile profile(settings);
    openomada::application::InformSnapshot snapshot;
    snapshot.need_reply = false;
    snapshot.extra_body_members.push_back({"wSettings_2G", R"({"ch":"11"})"});

    openomada::application::WirelessClientState client;
    client.mac = mac("aa:bb:cc:dd:ee:ff");
    client.ssid = "guest";
    snapshot.extra_body_members.push_back({"clients", openomada::application::client_stats_json({client})});

    const std::string body = openomada::application::build_inform_body_json(profile, snapshot);
    auto document = openomada::protocol::JsonDocument::parse(body);

    require(document.valid(), "inform body parses");
    require(openomada::protocol::object_member(document.get(), "wSettings_2G") != nullptr, "wireless field included");
    auto* clients = openomada::protocol::object_member(document.get(), "clients");
    auto* item = json_object_array_get_idx(clients, 0);
    require(openomada::protocol::json_string(openomada::protocol::object_member(item, "mac")).value_or("") == "AA-BB-CC-DD-EE-FF", "client MAC in inform");
}

} // namespace

int main() {
    test_maps_openwrt_wireless_status_to_omada_inform_members();
    test_extracts_hostapd_interfaces_from_wireless_status();
    test_maps_hostapd_clients_to_wireless_client_state();
    test_hostapd_augments_ssid_counts_and_radio_traffic();
    test_inform_body_accepts_openwrt_extra_members_and_clients();
    std::cout << "openomada-openwrt-telemetry-tests passed\n";
    return 0;
}
