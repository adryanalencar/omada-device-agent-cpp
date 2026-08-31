#include "openomada/application/client_state.hpp"

#include "openomada/protocol/ecsp_message.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <map>
#include <sstream>

namespace openomada::application {
namespace {

std::string quoted(std::string_view value) {
    std::string out = "\"";
    out += protocol::json_escape(value);
    out += "\"";
    return out;
}

std::vector<std::string> split_ws(std::string_view line) {
    std::vector<std::string> parts;
    std::size_t offset = 0;
    while (offset < line.size()) {
        while (offset < line.size() && std::isspace(static_cast<unsigned char>(line[offset])) != 0) {
            ++offset;
        }
        const std::size_t begin = offset;
        while (offset < line.size() && std::isspace(static_cast<unsigned char>(line[offset])) == 0) {
            ++offset;
        }
        if (offset > begin) {
            parts.emplace_back(line.substr(begin, offset - begin));
        }
    }
    return parts;
}

std::optional<std::int64_t> parse_int64(std::string_view text) noexcept {
    std::string owned(text);
    char* end = nullptr;
    errno = 0;
    const long long value = std::strtoll(owned.c_str(), &end, 10);
    if (errno != 0 || end == owned.c_str() || (end != nullptr && *end != '\0')) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(value);
}

WirelessClientState merge_client(const WirelessClientState& base, const WirelessClientState& overlay) {
    WirelessClientState merged = base;
    if (!overlay.ipv4.empty()) {
        merged.ipv4 = overlay.ipv4;
    }
    if (!overlay.ipv6.empty()) {
        merged.ipv6 = overlay.ipv6;
    }
    if (!overlay.hostname.empty()) {
        merged.hostname = overlay.hostname;
    }
    if (!overlay.ssid.empty()) {
        merged.ssid = overlay.ssid;
    }
    if (overlay.radio.has_value()) {
        merged.radio = overlay.radio;
    }
    if (overlay.rssi.has_value()) {
        merged.rssi = overlay.rssi;
    }
    if (overlay.snr.has_value()) {
        merged.snr = overlay.snr;
    }
    if (overlay.vlan_id.has_value()) {
        merged.vlan_id = overlay.vlan_id;
    }
    if (overlay.portal_state != ClientPortalState::Unknown) {
        merged.portal_state = overlay.portal_state;
    }
    if (overlay.rx_bytes != 0) {
        merged.rx_bytes = overlay.rx_bytes;
    }
    if (overlay.tx_bytes != 0) {
        merged.tx_bytes = overlay.tx_bytes;
    }
    if (overlay.rx_packets.has_value()) {
        merged.rx_packets = overlay.rx_packets;
    }
    if (overlay.tx_packets.has_value()) {
        merged.tx_packets = overlay.tx_packets;
    }
    if (overlay.rx_rate.has_value()) {
        merged.rx_rate = overlay.rx_rate;
    }
    if (overlay.tx_rate.has_value()) {
        merged.tx_rate = overlay.tx_rate;
    }
    if (overlay.association_time.has_value()) {
        merged.association_time = overlay.association_time;
    }
    return merged;
}

void append_uint_field(std::string& out, const char* key, std::uint64_t value) {
    out += ",\"";
    out += key;
    out += "\":";
    out += std::to_string(value);
}

void append_int_field(std::string& out, const char* key, std::int64_t value) {
    out += ",\"";
    out += key;
    out += "\":";
    out += std::to_string(value);
}

} // namespace

const char* to_wire_string(ClientPortalState state) noexcept {
    switch (state) {
    case ClientPortalState::Unknown:
        return "unknown";
    case ClientPortalState::Unauthenticated:
        return "unauthenticated";
    case ClientPortalState::Authenticating:
        return "authenticating";
    case ClientPortalState::Authenticated:
        return "authenticated";
    case ClientPortalState::Expired:
        return "expired";
    case ClientPortalState::Blocked:
        return "blocked";
    }
    return "unknown";
}

std::int32_t radio_id_for_band(RadioBand band) noexcept {
    switch (band) {
    case RadioBand::TwoG:
        return 0;
    case RadioBand::FiveG:
        return 1;
    case RadioBand::FiveG2:
        return 2;
    case RadioBand::SixG:
        return 3;
    }
    return 0;
}

DhcpLeaseParseResult parse_dnsmasq_leases(std::string_view text) noexcept {
    DhcpLeaseParseResult result;
    std::size_t line_no = 0;
    std::size_t offset = 0;
    while (offset <= text.size()) {
        ++line_no;
        const std::size_t newline = text.find('\n', offset);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
        const std::string_view line = text.substr(offset, end - offset);
        const auto parts = split_ws(line);
        if (!parts.empty() && !parts[0].empty() && parts[0][0] != '#') {
            if (parts.size() < 4) {
                result.error = "invalid dnsmasq lease line " + std::to_string(line_no);
                return result;
            }
            auto expires = parse_int64(parts[0]);
            auto mac = domain::MacAddress::parse(parts[1]);
            if (!expires.has_value() || !mac.has_value()) {
                result.error = "invalid dnsmasq lease line " + std::to_string(line_no);
                return result;
            }
            DhcpLease lease;
            lease.expires_at = *expires;
            lease.mac = *mac;
            lease.ipv4 = parts[2];
            if (parts[3] != "*") {
                lease.hostname = parts[3];
            }
            if (parts.size() > 4 && parts[4] != "*") {
                lease.client_id = parts[4];
            }
            result.leases.push_back(std::move(lease));
        }
        if (newline == std::string_view::npos) {
            break;
        }
        offset = newline + 1;
    }
    result.ok = true;
    return result;
}

std::vector<WirelessClientState> clients_from_dhcp_leases(const std::vector<DhcpLease>& leases) {
    std::map<std::string, DhcpLease> by_mac;
    for (const auto& lease : leases) {
        const std::string mac = lease.mac.normalized();
        auto existing = by_mac.find(mac);
        if (existing == by_mac.end() || lease.expires_at >= existing->second.expires_at) {
            by_mac[mac] = lease;
        }
    }

    std::vector<WirelessClientState> clients;
    clients.reserve(by_mac.size());
    for (const auto& item : by_mac) {
        WirelessClientState client;
        client.mac = item.second.mac;
        client.ipv4 = item.second.ipv4;
        client.hostname = item.second.hostname;
        clients.push_back(std::move(client));
    }
    return clients;
}

std::vector<WirelessClientState> merge_wireless_client_states(
    const std::vector<WirelessClientState>& base,
    const std::vector<WirelessClientState>& overlay
) {
    std::map<std::string, WirelessClientState> by_mac;
    for (const auto& client : base) {
        by_mac[client.mac.normalized()] = client;
    }
    for (const auto& client : overlay) {
        const std::string mac = client.mac.normalized();
        auto existing = by_mac.find(mac);
        by_mac[mac] = existing == by_mac.end() ? client : merge_client(existing->second, client);
    }

    std::vector<WirelessClientState> merged;
    merged.reserve(by_mac.size());
    for (const auto& item : by_mac) {
        merged.push_back(item.second);
    }
    return merged;
}

std::vector<WirelessClientState> merge_associated_wireless_client_states(
    const std::vector<WirelessClientState>& associated_clients,
    const std::vector<WirelessClientState>& metadata_clients
) {
    std::map<std::string, WirelessClientState> by_mac;
    for (const auto& client : associated_clients) {
        by_mac[client.mac.normalized()] = client;
    }
    for (const auto& client : metadata_clients) {
        const std::string mac = client.mac.normalized();
        auto existing = by_mac.find(mac);
        if (existing != by_mac.end()) {
            existing->second = merge_client(existing->second, client);
        }
    }

    std::vector<WirelessClientState> merged;
    merged.reserve(by_mac.size());
    for (const auto& item : by_mac) {
        merged.push_back(item.second);
    }
    return merged;
}

std::string client_stats_json(const std::vector<WirelessClientState>& clients) {
    std::string out = "[";
    for (std::size_t index = 0; index < clients.size(); ++index) {
        const auto& client = clients[index];
        if (index != 0) {
            out.push_back(',');
        }
        out += "{\"mac\":";
        out += quoted(client.mac.omada());
        if (!client.ipv4.empty()) {
            out += ",\"ip\":";
            out += quoted(client.ipv4);
        }
        if (!client.ipv6.empty()) {
            out += ",\"ipv6List\":[";
            for (std::size_t ipv6_index = 0; ipv6_index < client.ipv6.size(); ++ipv6_index) {
                if (ipv6_index != 0) {
                    out.push_back(',');
                }
                out += quoted(client.ipv6[ipv6_index]);
            }
            out.push_back(']');
        }
        if (!client.hostname.empty()) {
            out += ",\"name\":";
            out += quoted(client.hostname);
        }
        if (!client.ssid.empty()) {
            out += ",\"ssid\":";
            out += quoted(client.ssid);
        }
        if (client.radio.has_value()) {
            append_int_field(out, "rid", radio_id_for_band(*client.radio));
        }
        if (client.rssi.has_value()) {
            append_int_field(out, "rssi", *client.rssi);
        }
        if (client.snr.has_value()) {
            append_int_field(out, "snr", *client.snr);
        }
        if (client.vlan_id.has_value()) {
            append_int_field(out, "vid", *client.vlan_id);
        }
        if (client.portal_state != ClientPortalState::Unknown) {
            out += ",\"portalStatus\":";
            out += quoted(to_wire_string(client.portal_state));
        }
        append_uint_field(out, "down", client.rx_bytes);
        append_uint_field(out, "up", client.tx_bytes);
        if (client.rx_packets.has_value()) {
            append_uint_field(out, "rxP", *client.rx_packets);
        }
        if (client.tx_packets.has_value()) {
            append_uint_field(out, "txP", *client.tx_packets);
        }
        if (client.rx_rate.has_value()) {
            append_uint_field(out, "rxR", *client.rx_rate);
        }
        if (client.tx_rate.has_value()) {
            append_uint_field(out, "txR", *client.tx_rate);
        }
        if (client.association_time.has_value()) {
            append_uint_field(out, "aTime", *client.association_time);
        }
        out.push_back('}');
    }
    out.push_back(']');
    return out;
}

} // namespace openomada::application
