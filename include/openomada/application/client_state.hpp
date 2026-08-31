#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openomada/application/configuration.hpp"
#include "openomada/domain/mac_address.hpp"

namespace openomada::application {

enum class ClientPortalState {
    Unknown,
    Unauthenticated,
    Authenticating,
    Authenticated,
    Expired,
    Blocked,
};

const char* to_wire_string(ClientPortalState state) noexcept;
std::int32_t radio_id_for_band(RadioBand band) noexcept;

struct WirelessClientState {
    domain::MacAddress mac{};
    std::string ipv4{};
    std::vector<std::string> ipv6{};
    std::string hostname{};
    std::string ssid{};
    std::optional<RadioBand> radio{};
    std::optional<std::int64_t> rssi{};
    std::optional<std::int64_t> snr{};
    std::optional<std::int64_t> vlan_id{};
    ClientPortalState portal_state{ClientPortalState::Unknown};
    std::uint64_t rx_bytes{0};
    std::uint64_t tx_bytes{0};
    std::optional<std::uint64_t> rx_packets{};
    std::optional<std::uint64_t> tx_packets{};
    std::optional<std::uint64_t> rx_rate{};
    std::optional<std::uint64_t> tx_rate{};
    std::optional<std::uint64_t> association_time{};
};

struct DhcpLease {
    std::int64_t expires_at{0};
    domain::MacAddress mac{};
    std::string ipv4{};
    std::string hostname{};
    std::string client_id{};
};

struct DhcpLeaseParseResult {
    bool ok{false};
    std::vector<DhcpLease> leases{};
    std::string error{};
};

DhcpLeaseParseResult parse_dnsmasq_leases(std::string_view text) noexcept;
std::vector<WirelessClientState> clients_from_dhcp_leases(const std::vector<DhcpLease>& leases);
std::vector<WirelessClientState> merge_wireless_client_states(const std::vector<WirelessClientState>& base, const std::vector<WirelessClientState>& overlay);
std::vector<WirelessClientState> merge_associated_wireless_client_states(
    const std::vector<WirelessClientState>& associated_clients,
    const std::vector<WirelessClientState>& metadata_clients
);
std::string client_stats_json(const std::vector<WirelessClientState>& clients);

} // namespace openomada::application
