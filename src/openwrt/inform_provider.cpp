#include "openomada/openwrt/inform_provider.hpp"

#include "openomada/application/client_state.hpp"
#include "openomada/openwrt/telemetry.hpp"

#include <fstream>
#include <sstream>
#include <utility>

namespace openomada::openwrt {
namespace {

std::string read_optional_file(const std::string& path) {
    std::ifstream input(path);
    if (!input.good()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void append_clients(
    std::vector<application::WirelessClientState>* out,
    const std::vector<application::WirelessClientState>& clients
) {
    if (out == nullptr) {
        return;
    }
    out->insert(out->end(), clients.begin(), clients.end());
}

} // namespace

OpenWrtInformProvider::OpenWrtInformProvider(
    OpenNdsExecutor& executor,
    OpenWrtInformProviderOptions options
) : executor_(executor), options_(std::move(options)) {}

application::InformSnapshot OpenWrtInformProvider::build(
    bool need_reply,
    std::uint64_t uptime_seconds
) {
    application::InformSnapshot snapshot;
    snapshot.need_reply = need_reply;
    snapshot.uptime_seconds = uptime_seconds;

    const auto status = executor_.run({"ubus", "call", "network.wireless", "status"});
    if (!status.ok || status.output.empty()) {
        return snapshot;
    }

    auto interfaces = openwrt_wireless_interfaces_from_status_json(status.output);
    std::vector<std::string> hostapd_json_by_interface;
    std::vector<application::WirelessClientState> associated_clients;
    if (interfaces.ok) {
        hostapd_json_by_interface.reserve(interfaces.interfaces.size());
        for (const auto& interface : interfaces.interfaces) {
            const auto hostapd = executor_.run({"ubus", "call", "hostapd." + interface.ifname, "get_clients"});
            hostapd_json_by_interface.push_back(hostapd.ok ? hostapd.output : "{}");
            if (!hostapd.ok) {
                continue;
            }
            auto parsed_clients = hostapd_client_states_from_json(interface, hostapd.output);
            if (parsed_clients.ok) {
                append_clients(&associated_clients, parsed_clients.clients);
            }
        }
    }

    const auto wireless = openwrt_wireless_inform_from_status_and_hostapd_json(
        status.output,
        hostapd_json_by_interface
    );
    if (wireless.ok) {
        snapshot.extra_body_members.insert(
            snapshot.extra_body_members.end(),
            wireless.members.begin(),
            wireless.members.end()
        );
    }

    std::vector<application::WirelessClientState> metadata_clients;
    const std::string leases = read_optional_file(options_.dhcp_leases_path);
    if (!leases.empty()) {
        auto parsed_leases = application::parse_dnsmasq_leases(leases);
        if (parsed_leases.ok) {
            metadata_clients = application::clients_from_dhcp_leases(parsed_leases.leases);
        }
    }
    if (options_.include_opennds) {
        const auto opennds = executor_.run({"ndsctl", "json"});
        if (opennds.ok && !opennds.output.empty()) {
            const auto parsed_opennds = opennds_clients_from_json(opennds.output);
            if (parsed_opennds.ok) {
                append_clients(&metadata_clients, parsed_opennds.clients);
            }
        }
    }

    auto merged_clients = application::merge_associated_wireless_client_states(
        associated_clients,
        metadata_clients
    );
    if (!merged_clients.empty()) {
        snapshot.extra_body_members.push_back({
            "clientStats",
            application::client_stats_json(merged_clients),
        });
    }
    return snapshot;
}

} // namespace openomada::openwrt
