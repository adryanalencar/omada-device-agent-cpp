#include "openomada/application/inform.hpp"

#include "openomada/protocol/ecsp_message.hpp"

#include <utility>

namespace openomada::application {
namespace {

std::string quoted(std::string_view value) {
    std::string out = "\"";
    out += protocol::json_escape(value);
    out += "\"";
    return out;
}

protocol::EcspHeader inform_header(
    const AgentSettings& settings,
    std::uint32_t seq,
    const std::string& controller_id,
    std::uint64_t timestamp_ms
) {
    protocol::EcspHeader header{
        seq,
        settings.ecsp_version,
        settings.ecsp_ver_cap,
        "ap",
        settings.mac,
        protocol::MessageType::InformRequest,
        0,
        controller_id,
        std::nullopt,
    };
    if (timestamp_ms != 0) {
        header.timestamp = timestamp_ms;
    }
    return header;
}

} // namespace

StaticInformProvider::StaticInformProvider(LanObservation lan) : lan_(std::move(lan)) {}

InformSnapshot StaticInformProvider::build(bool need_reply, std::uint64_t uptime_seconds) {
    InformSnapshot snapshot;
    snapshot.need_reply = need_reply;
    snapshot.uptime_seconds = uptime_seconds;
    snapshot.lan = lan_;
    return snapshot;
}

std::string build_inform_body_json(
    const domain::AccessPointProfile& profile,
    const InformSnapshot& snapshot
) {
    std::string body;
    body.reserve(512);
    body += "{\"needReply\":";
    body += snapshot.need_reply ? "1" : "0";
    body += ",\"deviceInfo\":";
    body += profile.inform_device_info_json(snapshot.uptime_seconds);
    body += ",\"lanInfo\":{\"rate\":";
    body += quoted(snapshot.lan.rate);
    body += ",\"duplex\":";
    body += std::to_string(snapshot.lan.duplex);
    body += ",\"port\":";
    body += quoted(snapshot.lan.port);
    body += "}}";
    return body;
}

std::string build_inform_request_json(
    const AgentSettings& settings,
    const domain::AccessPointProfile& profile,
    std::uint32_t seq,
    const std::string& controller_id,
    const InformSnapshot& snapshot,
    std::uint64_t timestamp_ms
) {
    return protocol::build_message_json(
        inform_header(settings, seq, controller_id, timestamp_ms),
        build_inform_body_json(profile, snapshot)
    );
}

} // namespace openomada::application
