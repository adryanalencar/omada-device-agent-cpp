#include "openomada/openwrt/telemetry.hpp"

#include "openomada/platform/capabilities.hpp"
#include "openomada/protocol/ecsp_message.hpp"
#include "openomada/protocol/json.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <json-c/json.h>
#include <map>

namespace openomada::openwrt {
namespace {

struct JsonField {
    std::string key{};
    std::string value{};
};

using JsonFields = std::vector<JsonField>;

std::string quoted(std::string_view value) {
    std::string out = "\"";
    out += protocol::json_escape(value);
    out += "\"";
    return out;
}

std::string object_json(const JsonFields& fields) {
    std::string out = "{";
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0) {
            out.push_back(',');
        }
        out += quoted(fields[index].key);
        out.push_back(':');
        out += fields[index].value;
    }
    out.push_back('}');
    return out;
}

std::string array_json(const std::vector<std::string>& values) {
    std::string out = "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            out.push_back(',');
        }
        out += values[index];
    }
    out.push_back(']');
    return out;
}

bool is_mapping(json_object* object) noexcept {
    return object != nullptr && json_object_is_type(object, json_type_object);
}

std::optional<std::string> optional_string_any(json_object* value) {
    if (value == nullptr || json_object_is_type(value, json_type_null)) {
        return std::nullopt;
    }
    const char* text = json_object_get_string(value);
    if (text == nullptr || *text == '\0') {
        return std::nullopt;
    }
    return std::string(text);
}

std::optional<std::int64_t> optional_int_any(json_object* value) noexcept {
    if (value == nullptr || json_object_is_type(value, json_type_null)) {
        return std::nullopt;
    }
    if (json_object_is_type(value, json_type_string)) {
        const char* text = json_object_get_string(value);
        if (text == nullptr || *text == '\0') {
            return std::nullopt;
        }
        char* end = nullptr;
        errno = 0;
        const long long parsed = std::strtoll(text, &end, 10);
        if (errno != 0 || end == text || (end != nullptr && *end != '\0')) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(parsed);
    }
    if (!json_object_is_type(value, json_type_int) &&
        !json_object_is_type(value, json_type_double) &&
        !json_object_is_type(value, json_type_boolean)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(json_object_get_int64(value));
}

std::uint64_t counter(json_object* object, const char* first, const char* second = nullptr) noexcept {
    auto value = optional_int_any(protocol::object_member(object, first));
    if (!value.has_value() && second != nullptr) {
        value = optional_int_any(protocol::object_member(object, second));
    }
    if (!value.has_value() || *value < 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(*value);
}

std::int64_t station_count(json_object* interface) noexcept {
    for (const char* key : {"stations", "assoclist", "clients"}) {
        auto* raw = protocol::object_member(interface, key);
        if (raw == nullptr) {
            continue;
        }
        if (json_object_is_type(raw, json_type_object)) {
            return static_cast<std::int64_t>(json_object_object_length(raw));
        }
        if (json_object_is_type(raw, json_type_array)) {
            return static_cast<std::int64_t>(json_object_array_length(raw));
        }
    }
    for (const char* key : {"num_sta", "staNum"}) {
        auto value = optional_int_any(protocol::object_member(interface, key));
        if (value.has_value()) {
            return std::max<std::int64_t>(0, *value);
        }
    }
    return 0;
}

std::optional<application::RadioBand> band_from_radio_name(std::string_view name) noexcept {
    const auto band = platform::radio_band_from_wire(name);
    if (band.has_value()) {
        return band;
    }
    if (name == "radio0") {
        return application::RadioBand::TwoG;
    }
    if (name == "radio1") {
        return application::RadioBand::FiveG;
    }
    if (name == "radio2") {
        return application::RadioBand::FiveG2;
    }
    if (name == "radio3") {
        return application::RadioBand::SixG;
    }
    return std::nullopt;
}

const char* suffix_for_band(application::RadioBand band) noexcept {
    switch (band) {
    case application::RadioBand::TwoG:
        return "2G";
    case application::RadioBand::FiveG:
        return "5G";
    case application::RadioBand::FiveG2:
        return "5G2";
    case application::RadioBand::SixG:
        return "6G";
    }
    return "2G";
}

std::vector<json_object*> interface_objects(json_object* radio) {
    std::vector<json_object*> interfaces;
    auto* raw = protocol::object_member(radio, "interfaces");
    if (json_object_is_type(raw, json_type_array)) {
        const std::size_t count = json_object_array_length(raw);
        interfaces.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            auto* item = json_object_array_get_idx(raw, index);
            if (is_mapping(item)) {
                interfaces.push_back(item);
            }
        }
    } else if (is_mapping(raw)) {
        json_object_object_foreach(raw, key, value) {
            (void)key;
            if (is_mapping(value)) {
                interfaces.push_back(value);
            }
        }
    }
    return interfaces;
}

std::string interface_ifname(json_object* interface) {
    auto* config = protocol::object_member(interface, "config");
    for (const char* key : {"ifname", "ifname_current", "section"}) {
        if (auto value = optional_string_any(protocol::object_member(interface, key))) {
            return *value;
        }
    }
    if (auto value = optional_string_any(protocol::object_member(config, "ifname"))) {
        return *value;
    }
    return {};
}

std::string interface_ssid(json_object* interface) {
    auto* config = protocol::object_member(interface, "config");
    if (auto value = optional_string_any(protocol::object_member(interface, "ssid"))) {
        return *value;
    }
    if (auto value = optional_string_any(protocol::object_member(config, "ssid"))) {
        return *value;
    }
    return {};
}

std::string wireless_info_json(json_object* radio, const std::vector<json_object*>& interfaces) {
    auto* config = protocol::object_member(radio, "config");
    JsonFields fields;
    if (auto value = optional_string_any(protocol::object_member(config, "channel"))) {
        fields.push_back({"ch", quoted(*value)});
    } else if (auto value = optional_string_any(protocol::object_member(radio, "channel"))) {
        fields.push_back({"ch", quoted(*value)});
    }
    if (auto value = optional_string_any(protocol::object_member(config, "htmode"))) {
        fields.push_back({"bw", quoted(*value)});
    } else if (auto value = optional_string_any(protocol::object_member(config, "bandwidth"))) {
        fields.push_back({"bw", quoted(*value)});
    } else if (auto value = optional_string_any(protocol::object_member(radio, "htmode"))) {
        fields.push_back({"bw", quoted(*value)});
    }
    if (auto value = optional_string_any(protocol::object_member(config, "hwmode"))) {
        fields.push_back({"rdMode", quoted(*value)});
    } else if (auto value = optional_string_any(protocol::object_member(config, "mode"))) {
        fields.push_back({"rdMode", quoted(*value)});
    } else if (auto value = optional_string_any(protocol::object_member(radio, "hwmode"))) {
        fields.push_back({"rdMode", quoted(*value)});
    }
    if (auto value = optional_string_any(protocol::object_member(config, "txpower"))) {
        fields.push_back({"txPower", quoted(*value)});
    } else if (auto value = optional_string_any(protocol::object_member(radio, "txpower"))) {
        fields.push_back({"txPower", quoted(*value)});
    }
    std::int64_t stations = 0;
    for (auto* interface : interfaces) {
        stations += station_count(interface);
    }
    if (stations > 0) {
        fields.push_back({"staNum", std::to_string(stations)});
    }
    return fields.empty() ? std::string() : object_json(fields);
}

std::string ssid_stats_json(json_object* interface) {
    const std::string ssid = interface_ssid(interface);
    if (ssid.empty()) {
        return {};
    }

    JsonFields fields;
    fields.push_back({"ssid", quoted(ssid)});
    fields.push_back({"clntNum", std::to_string(station_count(interface))});
    auto* config = protocol::object_member(interface, "config");
    std::string bssid;
    if (auto value = optional_string_any(protocol::object_member(interface, "bssid"))) {
        bssid = *value;
    } else if (auto value = optional_string_any(protocol::object_member(config, "bssid"))) {
        bssid = *value;
    } else if (auto value = optional_string_any(protocol::object_member(config, "macaddr"))) {
        bssid = *value;
    }
    if (!bssid.empty()) {
        if (auto mac = domain::MacAddress::parse(bssid)) {
            fields.push_back({"bssid", quoted(mac->omada())});
        } else {
            fields.push_back({"bssid", quoted(bssid)});
        }
    }
    auto* counters = protocol::object_member(interface, "statistics");
    if (!is_mapping(counters)) {
        counters = protocol::object_member(interface, "stats");
    }
    if (is_mapping(counters)) {
        const auto down = counter(counters, "tx_bytes", "txByte");
        const auto up = counter(counters, "rx_bytes", "rxByte");
        const auto down_pkts = counter(counters, "tx_packets", "txPackets");
        const auto up_pkts = counter(counters, "rx_packets", "rxPackets");
        if (down == 0) {
            const auto fallback = counter(counters, "tx");
            if (fallback != 0) {
                fields.push_back({"down", std::to_string(fallback)});
            }
        } else {
            fields.push_back({"down", std::to_string(down)});
        }
        if (up == 0) {
            const auto fallback = counter(counters, "rx");
            if (fallback != 0) {
                fields.push_back({"up", std::to_string(fallback)});
            }
        } else {
            fields.push_back({"up", std::to_string(up)});
        }
        if (down_pkts != 0) {
            fields.push_back({"downPkts", std::to_string(down_pkts)});
        }
        if (up_pkts != 0) {
            fields.push_back({"upPkts", std::to_string(up_pkts)});
        }
    }
    return object_json(fields);
}

std::string field_key(std::string_view prefix, application::RadioBand band) {
    std::string key(prefix);
    key.push_back('_');
    key += suffix_for_band(band);
    return key;
}

void set_member(std::vector<application::JsonBodyMember>& members, std::string key, std::string json) {
    for (auto& member : members) {
        if (member.key == key) {
            member.json = std::move(json);
            return;
        }
    }
    members.push_back({std::move(key), std::move(json)});
}

std::string member_json(const std::vector<application::JsonBodyMember>& members, const std::string& key) {
    for (const auto& member : members) {
        if (member.key == key) {
            return member.json;
        }
    }
    return {};
}

std::string merge_ssid_stats_array_json(std::string_view existing_json, const std::string& stats_json) {
    std::vector<std::string> items;
    std::string replacement_ssid;
    protocol::JsonDocument stats = protocol::JsonDocument::parse(stats_json);
    if (stats.valid()) {
        replacement_ssid = optional_string_any(protocol::object_member(stats.get(), "ssid")).value_or("");
    }

    protocol::JsonDocument existing = protocol::JsonDocument::parse("{\"items\":" + std::string(existing_json.empty() ? "[]" : existing_json) + "}");
    if (existing.valid()) {
        auto* array = protocol::object_member(existing.get(), "items");
        if (json_object_is_type(array, json_type_array)) {
            const std::size_t count = json_object_array_length(array);
            for (std::size_t index = 0; index < count; ++index) {
                auto* item = json_object_array_get_idx(array, index);
                if (!is_mapping(item)) {
                    continue;
                }
                const std::string ssid = optional_string_any(protocol::object_member(item, "ssid")).value_or("");
                if (!replacement_ssid.empty() && ssid == replacement_ssid) {
                    continue;
                }
                const char* rendered = json_object_to_json_string_ext(item, JSON_C_TO_STRING_PLAIN);
                if (rendered != nullptr) {
                    items.emplace_back(rendered);
                }
            }
        }
    }
    items.push_back(stats_json);
    return array_json(items);
}

std::uint64_t object_uint(json_object* object, const char* key) noexcept {
    auto value = optional_int_any(protocol::object_member(object, key));
    if (!value.has_value() || *value < 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(*value);
}

std::string merge_radio_traffic_json(std::string_view existing_json, const std::string& addition_json) {
    std::map<std::string, std::uint64_t> values;
    for (const char* key : {"tx", "rx", "txP", "rxP", "txDP", "rxDP", "txEP", "rxEP", "txRP", "rxRP"}) {
        values[key] = 0;
    }
    protocol::JsonDocument existing = protocol::JsonDocument::parse(existing_json.empty() ? "{}" : existing_json);
    if (existing.valid()) {
        for (auto& item : values) {
            item.second += object_uint(existing.get(), item.first.c_str());
        }
    }
    protocol::JsonDocument addition = protocol::JsonDocument::parse(addition_json);
    if (addition.valid()) {
        for (auto& item : values) {
            item.second += object_uint(addition.get(), item.first.c_str());
        }
    }
    return object_json({
        {"tx", std::to_string(values["tx"])},
        {"rx", std::to_string(values["rx"])},
        {"txP", std::to_string(values["txP"])},
        {"rxP", std::to_string(values["rxP"])},
        {"txDP", std::to_string(values["txDP"])},
        {"rxDP", std::to_string(values["rxDP"])},
        {"txEP", std::to_string(values["txEP"])},
        {"rxEP", std::to_string(values["rxEP"])},
        {"txRP", std::to_string(values["txRP"])},
        {"rxRP", std::to_string(values["rxRP"])},
    });
}

std::string hostapd_ssid_stats_json(const std::string& ssid, json_object* hostapd_status) {
    auto* raw_clients = protocol::object_member(hostapd_status, "clients");
    if (!is_mapping(raw_clients)) {
        return {};
    }
    std::uint64_t down = 0;
    std::uint64_t up = 0;
    std::uint64_t down_packets = 0;
    std::uint64_t up_packets = 0;
    std::size_t client_count = 0;
    json_object_object_foreach(raw_clients, raw_mac, raw_client) {
        (void)raw_mac;
        if (!is_mapping(raw_client)) {
            continue;
        }
        ++client_count;
        auto* bytes = protocol::object_member(raw_client, "bytes");
        auto* packets = protocol::object_member(raw_client, "packets");
        down += counter(bytes, "tx", "tx_bytes");
        up += counter(bytes, "rx", "rx_bytes");
        down_packets += counter(packets, "tx", "tx_packets");
        up_packets += counter(packets, "rx", "rx_packets");
    }
    JsonFields fields;
    fields.push_back({"ssid", quoted(ssid)});
    fields.push_back({"clntNum", std::to_string(client_count)});
    if (down != 0) {
        fields.push_back({"down", std::to_string(down)});
    }
    if (up != 0) {
        fields.push_back({"up", std::to_string(up)});
    }
    if (down_packets != 0) {
        fields.push_back({"downPkts", std::to_string(down_packets)});
    }
    if (up_packets != 0) {
        fields.push_back({"upPkts", std::to_string(up_packets)});
    }
    return object_json(fields);
}

std::string radio_traffic_json(json_object* hostapd_status) {
    auto* raw_clients = protocol::object_member(hostapd_status, "clients");
    std::uint64_t down = 0;
    std::uint64_t up = 0;
    std::uint64_t down_packets = 0;
    std::uint64_t up_packets = 0;
    if (is_mapping(raw_clients)) {
        json_object_object_foreach(raw_clients, raw_mac, raw_client) {
            (void)raw_mac;
            if (!is_mapping(raw_client)) {
                continue;
            }
            auto* bytes = protocol::object_member(raw_client, "bytes");
            auto* packets = protocol::object_member(raw_client, "packets");
            down += counter(bytes, "tx", "tx_bytes");
            up += counter(bytes, "rx", "rx_bytes");
            down_packets += counter(packets, "tx", "tx_packets");
            up_packets += counter(packets, "rx", "rx_packets");
        }
    }
    return object_json({
        {"tx", std::to_string(down)},
        {"rx", std::to_string(up)},
        {"txP", std::to_string(down_packets)},
        {"rxP", std::to_string(up_packets)},
        {"txDP", "0"},
        {"rxDP", "0"},
        {"txEP", "0"},
        {"rxEP", "0"},
        {"txRP", "0"},
        {"rxRP", "0"},
    });
}

std::string merge_sta_num(std::string_view current_json, std::size_t station_count_value) {
    protocol::JsonDocument document = protocol::JsonDocument::parse(current_json);
    JsonFields fields;
    std::int64_t current_sta = 0;
    if (document.valid()) {
        json_object_object_foreach(document.get(), key, value) {
            if (std::string_view(key) == "staNum") {
                auto parsed = optional_int_any(value);
                current_sta = parsed.value_or(0);
                continue;
            }
            const char* rendered = json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN);
            if (rendered != nullptr) {
                fields.push_back({key, rendered});
            }
        }
    }
    const std::uint64_t merged = std::max<std::uint64_t>(
        static_cast<std::uint64_t>(std::max<std::int64_t>(0, current_sta)),
        static_cast<std::uint64_t>(station_count_value)
    );
    fields.push_back({"staNum", std::to_string(merged)});
    return object_json(fields);
}

std::size_t hostapd_client_count(json_object* hostapd_status) noexcept {
    auto* raw_clients = protocol::object_member(hostapd_status, "clients");
    return is_mapping(raw_clients)
        ? static_cast<std::size_t>(json_object_object_length(raw_clients))
        : 0;
}

} // namespace

WirelessInformResult openwrt_wireless_inform_from_status_json(std::string_view status_json) noexcept {
    WirelessInformResult result;
    protocol::JsonDocument document = protocol::JsonDocument::parse(status_json);
    if (!document.valid()) {
        result.error = "wireless status must be a JSON object";
        return result;
    }

    json_object_object_foreach(document.get(), radio_name, raw_radio) {
        if (!is_mapping(raw_radio)) {
            continue;
        }
        auto band = band_from_radio_name(radio_name);
        if (!band.has_value()) {
            continue;
        }
        const auto interfaces = interface_objects(raw_radio);
        if (const std::string info = wireless_info_json(raw_radio, interfaces); !info.empty()) {
            result.members.push_back({field_key("wSettings", *band), info});
        }
        std::vector<std::string> ssid_stats;
        for (auto* interface : interfaces) {
            if (const std::string stats = ssid_stats_json(interface); !stats.empty()) {
                ssid_stats.push_back(stats);
            }
        }
        if (!ssid_stats.empty()) {
            result.members.push_back({field_key("ssidStats", *band), array_json(ssid_stats)});
        }
    }

    result.ok = true;
    return result;
}

WirelessInterfaceParseResult openwrt_wireless_interfaces_from_status_json(std::string_view status_json) noexcept {
    WirelessInterfaceParseResult result;
    protocol::JsonDocument document = protocol::JsonDocument::parse(status_json);
    if (!document.valid()) {
        result.error = "wireless status must be a JSON object";
        return result;
    }

    json_object_object_foreach(document.get(), radio_name, raw_radio) {
        if (!is_mapping(raw_radio)) {
            continue;
        }
        const auto band = band_from_radio_name(radio_name);
        for (auto* interface : interface_objects(raw_radio)) {
            OpenWrtWirelessInterface observed;
            observed.ifname = interface_ifname(interface);
            if (observed.ifname.empty()) {
                continue;
            }
            observed.ssid = interface_ssid(interface);
            observed.band = band;
            result.interfaces.push_back(std::move(observed));
        }
    }

    result.ok = true;
    return result;
}

HostapdClientResult hostapd_client_states_from_json(
    const OpenWrtWirelessInterface& interface,
    std::string_view hostapd_json
) noexcept {
    HostapdClientResult result;
    protocol::JsonDocument document = protocol::JsonDocument::parse(hostapd_json);
    if (!document.valid()) {
        result.error = "hostapd status must be a JSON object";
        return result;
    }
    auto* clients = protocol::object_member(document.get(), "clients");
    if (!is_mapping(clients)) {
        result.ok = true;
        return result;
    }

    json_object_object_foreach(clients, raw_mac, raw_client) {
        if (!is_mapping(raw_client)) {
            continue;
        }
        auto mac = domain::MacAddress::parse(raw_mac);
        if (!mac.has_value()) {
            continue;
        }
        application::WirelessClientState client;
        client.mac = *mac;
        client.ssid = interface.ssid;
        client.radio = interface.band;
        client.rssi = optional_int_any(protocol::object_member(raw_client, "signal"));
        if (!client.rssi.has_value()) {
            client.rssi = optional_int_any(protocol::object_member(raw_client, "rssi"));
        }
        client.snr = optional_int_any(protocol::object_member(raw_client, "snr"));
        auto* bytes = protocol::object_member(raw_client, "bytes");
        auto* packets = protocol::object_member(raw_client, "packets");
        auto* rate = protocol::object_member(raw_client, "rate");
        client.rx_bytes = counter(bytes, "tx", "tx_bytes");
        client.tx_bytes = counter(bytes, "rx", "rx_bytes");
        const auto rx_packets = counter(packets, "rx", "rx_packets");
        const auto tx_packets = counter(packets, "tx", "tx_packets");
        if (rx_packets != 0) {
            client.rx_packets = rx_packets;
        }
        if (tx_packets != 0) {
            client.tx_packets = tx_packets;
        }
        const auto rx_rate = counter(rate, "rx");
        const auto tx_rate = counter(rate, "tx");
        if (rx_rate != 0) {
            client.rx_rate = rx_rate;
        } else if (auto raw = optional_int_any(protocol::object_member(raw_client, "rx_rate")); raw.has_value() && *raw >= 0) {
            client.rx_rate = static_cast<std::uint64_t>(*raw);
        }
        if (tx_rate != 0) {
            client.tx_rate = tx_rate;
        } else if (auto raw = optional_int_any(protocol::object_member(raw_client, "tx_rate")); raw.has_value() && *raw >= 0) {
            client.tx_rate = static_cast<std::uint64_t>(*raw);
        }
        if (auto connected = optional_int_any(protocol::object_member(raw_client, "connected_time")); connected.has_value() && *connected >= 0) {
            client.association_time = static_cast<std::uint64_t>(*connected);
        } else if (auto association = optional_int_any(protocol::object_member(raw_client, "association_time")); association.has_value() && *association >= 0) {
            client.association_time = static_cast<std::uint64_t>(*association);
        } else if (auto atime = optional_int_any(protocol::object_member(raw_client, "aTime")); atime.has_value() && *atime >= 0) {
            client.association_time = static_cast<std::uint64_t>(*atime);
        }
        result.clients.push_back(std::move(client));
    }

    std::sort(result.clients.begin(), result.clients.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.mac.normalized() < rhs.mac.normalized();
    });
    result.ok = true;
    return result;
}

WirelessInformResult openwrt_wireless_inform_from_status_and_hostapd_json(
    std::string_view status_json,
    const std::vector<std::string>& hostapd_client_json_by_interface
) noexcept {
    auto payload = openwrt_wireless_inform_from_status_json(status_json);
    if (!payload.ok) {
        return payload;
    }
    auto interfaces = openwrt_wireless_interfaces_from_status_json(status_json);
    if (!interfaces.ok) {
        payload.ok = false;
        payload.error = interfaces.error;
        return payload;
    }

    const std::size_t count = std::min(interfaces.interfaces.size(), hostapd_client_json_by_interface.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto& interface = interfaces.interfaces[index];
        if (!interface.band.has_value() || interface.ssid.empty()) {
            continue;
        }
        protocol::JsonDocument hostapd = protocol::JsonDocument::parse(hostapd_client_json_by_interface[index]);
        if (!hostapd.valid()) {
            continue;
        }
        const std::string stats = hostapd_ssid_stats_json(interface.ssid, hostapd.get());
        if (stats.empty()) {
            continue;
        }
        const auto stats_key = field_key("ssidStats", *interface.band);
        set_member(payload.members, stats_key, merge_ssid_stats_array_json(member_json(payload.members, stats_key), stats));
        const auto settings_key = field_key("wSettings", *interface.band);
        const std::string current_settings = member_json(payload.members, settings_key);
        set_member(payload.members, settings_key, merge_sta_num(current_settings.empty() ? "{}" : current_settings, hostapd_client_count(hostapd.get())));
        const auto traffic_key = field_key("radioTraffic", *interface.band);
        set_member(
            payload.members,
            traffic_key,
            merge_radio_traffic_json(member_json(payload.members, traffic_key), radio_traffic_json(hostapd.get()))
        );
    }

    return payload;
}

} // namespace openomada::openwrt
