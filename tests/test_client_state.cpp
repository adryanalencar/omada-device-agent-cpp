#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "openomada/application/client_state.hpp"
#include "openomada/protocol/json.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

openomada::domain::MacAddress mac(const char* value) {
    auto parsed = openomada::domain::MacAddress::parse(value);
    require(parsed.has_value(), "MAC parses");
    return *parsed;
}

void test_parse_dnsmasq_leases_and_merge_latest_by_mac() {
    const auto parsed = openomada::application::parse_dnsmasq_leases(
        "1000 aa:bb:cc:dd:ee:ff 192.0.2.10 phone 01:aabb\n"
        "1200 aa-bb-cc-dd-ee-ff 192.0.2.11 phone-new *\n"
        "900 02:00:00:00:00:02 192.0.2.12 * *\n"
    );
    require(parsed.ok, parsed.error.c_str());

    const auto clients = openomada::application::clients_from_dhcp_leases(parsed.leases);

    require(clients.size() == 2, "two DHCP clients");
    require(clients[0].mac.normalized() == "02:00:00:00:00:02", "first sorted MAC");
    require(clients[0].hostname.empty(), "star hostname omitted");
    require(clients[1].mac.normalized() == "aa:bb:cc:dd:ee:ff", "merged MAC");
    require(clients[1].ipv4 == "192.0.2.11", "latest IP wins");
    require(clients[1].hostname == "phone-new", "latest hostname wins");
}

void test_merge_associated_filters_stale_metadata_clients() {
    openomada::application::WirelessClientState associated;
    associated.mac = mac("aa:bb:cc:dd:ee:ff");
    associated.ssid = "guest";
    associated.radio = openomada::application::RadioBand::TwoG;
    associated.rssi = -60;

    openomada::application::WirelessClientState metadata;
    metadata.mac = mac("AA-BB-CC-DD-EE-FF");
    metadata.ipv4 = "192.0.2.10";
    metadata.hostname = "phone";

    openomada::application::WirelessClientState stale;
    stale.mac = mac("02:00:00:00:00:02");
    stale.ipv4 = "192.0.2.20";
    stale.hostname = "stale";

    const auto merged = openomada::application::merge_associated_wireless_client_states(
        {associated},
        {metadata, stale}
    );

    require(merged.size() == 1, "stale metadata filtered");
    require(merged[0].mac.normalized() == "aa:bb:cc:dd:ee:ff", "associated retained");
    require(merged[0].ipv4 == "192.0.2.10", "metadata IP merged");
    require(merged[0].hostname == "phone", "metadata hostname merged");
    require(merged[0].ssid == "guest", "association SSID retained");
    require(merged[0].rssi.value_or(0) == -60, "association RSSI retained");
}

void test_client_stats_payload_uses_omada_mac_and_field_names() {
    openomada::application::WirelessClientState client;
    client.mac = mac("aa:bb:cc:dd:ee:ff");
    client.ipv4 = "192.0.2.10";
    client.hostname = "phone";
    client.ssid = "guest";
    client.radio = openomada::application::RadioBand::TwoG;
    client.rssi = -62;
    client.snr = 35;
    client.rx_bytes = 123;
    client.tx_bytes = 45;
    client.rx_packets = 4;
    client.tx_packets = 8;
    client.rx_rate = 6500;
    client.tx_rate = 7200;
    client.association_time = 12;

    const std::string payload = openomada::application::client_stats_json({client});

    auto document = openomada::protocol::JsonDocument::parse("{\"clients\":" + payload + "}");
    require(document.valid(), "payload parses");
    auto* clients = openomada::protocol::object_member(document.get(), "clients");
    auto* item = json_object_array_get_idx(clients, 0);
    require(openomada::protocol::json_string(openomada::protocol::object_member(item, "mac")).value_or("") == "AA-BB-CC-DD-EE-FF", "Omada MAC");
    require(openomada::protocol::json_string(openomada::protocol::object_member(item, "ip")).value_or("") == "192.0.2.10", "IP");
    require(openomada::protocol::json_string(openomada::protocol::object_member(item, "name")).value_or("") == "phone", "name");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "rid")).value_or(-1) == 0, "radio id");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "rssi")).value_or(0) == -62, "rssi");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "snr")).value_or(0) == 35, "snr");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "down")).value_or(-1) == 123, "down");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "up")).value_or(-1) == 45, "up");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "rxP")).value_or(-1) == 4, "rx packets");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "txP")).value_or(-1) == 8, "tx packets");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "rxR")).value_or(-1) == 6500, "rx rate");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "txR")).value_or(-1) == 7200, "tx rate");
    require(openomada::protocol::json_int(openomada::protocol::object_member(item, "aTime")).value_or(-1) == 12, "association time");
}

} // namespace

int main() {
    test_parse_dnsmasq_leases_and_merge_latest_by_mac();
    test_merge_associated_filters_stale_metadata_clients();
    test_client_stats_payload_uses_omada_mac_and_field_names();
    std::cout << "openomada-client-state-tests passed\n";
    return 0;
}
