#include "openomada/lifecycle/discovery.hpp"

#include "openomada/protocol/ecsp_builders.hpp"
#include "openomada/protocol/json.hpp"
#include "openomada/protocol/message_type.hpp"

namespace openomada::lifecycle {

PreAdoptRequest parse_pre_adopt_request(
    std::string_view payload,
    const std::string& current_controller_id,
    std::uint16_t default_manage_port
) {
    PreAdoptRequest result;
    result.controller_id = current_controller_id;
    result.adopt_port = default_manage_port;

    auto document = protocol::JsonDocument::parse(payload);
    if (!document.valid()) {
        result.error = "invalid ECSP JSON";
        return result;
    }

    const auto type = protocol::ecsp_header_type(document.get());
    if (!type.has_value()) {
        result.error = "missing ECSP header.type";
        return result;
    }
    result.is_pre_adopt = *type == protocol::to_underlying(protocol::MessageType::PreAdoptRequest);

    auto seq = protocol::ecsp_header_seq(document.get());
    if (seq.has_value()) {
        result.seq = *seq;
    }

    auto* header = protocol::object_member(document.get(), "header");
    auto learned_dest = protocol::json_string(protocol::object_member(header, "dest"));
    if (learned_dest.has_value() && !learned_dest->empty()) {
        if (learned_dest->size() == 24) {
            result.destination_id = *learned_dest;
        } else if (result.controller_id.empty()) {
            result.controller_id = *learned_dest;
        }
    }

    if (!result.is_pre_adopt) {
        result.ok = true;
        return result;
    }

    auto* body = protocol::ecsp_body(document.get());
    auto adopt_port = protocol::json_int(protocol::object_member(body, "adoptPort"));
    if (adopt_port.has_value() && *adopt_port > 0 && *adopt_port <= 65535) {
        result.adopt_port = static_cast<std::uint16_t>(*adopt_port);
    }
    result.ok = true;
    return result;
}

transport::TransportStatus send_discovery_once(
    transport::UdpDiscoveryTransport& transport,
    const transport::UdpEndpoint& endpoint,
    const application::AgentSettings& settings,
    const domain::AccessPointProfile& profile,
    std::uint32_t seq,
    bool managed_restart,
    std::uint64_t timestamp_ms
) {
    const std::string discovery = protocol::build_discovery_json(
        settings,
        profile,
        seq,
        managed_restart,
        timestamp_ms
    );
    return transport.send_payload_to(endpoint, discovery);
}

} // namespace openomada::lifecycle

