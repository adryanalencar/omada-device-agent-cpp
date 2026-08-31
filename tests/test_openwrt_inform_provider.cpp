#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>

#include "openomada/openwrt/inform_provider.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class ScriptedExecutor final : public openomada::openwrt::OpenNdsExecutor {
public:
    std::vector<std::pair<std::vector<std::string>, openomada::openwrt::OpenNdsCommandResult>> scripted{};

    openomada::openwrt::OpenNdsCommandResult run(
        const std::vector<std::string>& command,
        std::string_view input = {}
    ) override {
        (void)input;
        calls.push_back(command);
        for (const auto& item : scripted) {
            if (item.first == command) {
                return item.second;
            }
        }
        return {false, 127, {}, "unexpected command"};
    }

    std::vector<std::vector<std::string>> calls{};
};

std::string member_json(
    const std::vector<openomada::application::JsonBodyMember>& members,
    const std::string& key
) {
    for (const auto& member : members) {
        if (member.key == key) {
            return member.json;
        }
    }
    return {};
}

void write_file(const std::string& path, const std::string& data) {
    std::ofstream output(path);
    require(output.good(), "lease file opens");
    output << data;
}

void test_provider_collects_wireless_and_filters_stale_metadata_clients() {
    const std::string leases_path = "/tmp/openomada-leases-" + std::to_string(static_cast<long long>(::getpid()));
    write_file(
        leases_path,
        "2000000000 aa:bb:cc:dd:ee:ff phone 01:aa\n"
        "2000000000 02:00:00:00:00:02 stale 01:02\n"
    );

    ScriptedExecutor executor;
    executor.scripted.push_back({
        {"ubus", "call", "network.wireless", "status"},
        {true, 0, R"({
            "radio0": {
                "config": {"channel": "6", "htmode": "HT20"},
                "interfaces": [{
                    "ifname": "phy0-ap0",
                    "config": {"ssid": "Ubatuba - Wifi Grátis"}
                }]
            }
        })", {}},
    });
    executor.scripted.push_back({
        {"ubus", "call", "hostapd.phy0-ap0", "get_clients"},
        {true, 0, R"({
            "clients": {
                "aa:bb:cc:dd:ee:ff": {
                    "signal": -62,
                    "snr": 25,
                    "bytes": {"rx": 10, "tx": 20},
                    "packets": {"rx": 1, "tx": 2},
                    "rate": {"rx": 65000, "tx": 39000},
                    "connected_time": 12
                }
            }
        })", {}},
    });
    executor.scripted.push_back({
        {"ndsctl", "json"},
        {true, 0, R"({
            "clients": {
                "AA-BB-CC-DD-EE-FF": {"state": "Authenticated", "ip": "192.168.1.20"},
                "02-00-00-00-00-02": {"state": "Authenticated", "ip": "192.168.1.99"}
            }
        })", {}},
    });

    openomada::openwrt::OpenWrtInformProviderOptions options;
    options.dhcp_leases_path = leases_path;
    openomada::openwrt::OpenWrtInformProvider provider(executor, options);

    const auto snapshot = provider.build(true, 42);

    require(snapshot.need_reply, "need reply");
    require(snapshot.uptime_seconds == 42, "uptime");
    require(!member_json(snapshot.extra_body_members, "wSettings_2G").empty(), "wireless settings");
    require(!member_json(snapshot.extra_body_members, "ssidStats_2G").empty(), "ssid stats");
    require(!member_json(snapshot.extra_body_members, "radioTraffic_2G").empty(), "radio traffic");
    const std::string clients = member_json(snapshot.extra_body_members, "clientStats");
    require(clients.find("AA-BB-CC-DD-EE-FF") != std::string::npos, "associated client serialized in Omada MAC format");
    require(clients.find("02-00-00-00-00-02") == std::string::npos, "stale metadata client filtered");
    require(clients.find("authenticated") != std::string::npos, "portal state merged");
    (void)::unlink(leases_path.c_str());
}

void test_provider_degrades_to_minimal_snapshot_without_ubus() {
    ScriptedExecutor executor;
    openomada::openwrt::OpenWrtInformProvider provider(executor);

    const auto snapshot = provider.build(false, 7);

    require(!snapshot.need_reply, "need reply false");
    require(snapshot.uptime_seconds == 7, "uptime retained");
    require(snapshot.extra_body_members.empty(), "no extra members without ubus");
}

} // namespace

int main() {
    test_provider_collects_wireless_and_filters_stale_metadata_clients();
    test_provider_degrades_to_minimal_snapshot_without_ubus();
    std::cout << "openomada-openwrt-inform-provider-tests passed\n";
    return 0;
}
