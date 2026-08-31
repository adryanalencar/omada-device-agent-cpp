#include "openomada/protocol/ecsp_builders.hpp"

#include "openomada/protocol/ecsp_message.hpp"

namespace openomada::protocol {
namespace {

std::string destination_id(const application::AgentSettings& settings) {
    if (!settings.site_id.empty()) {
        return settings.site_id;
    }
    if (!settings.destination_controller_id.empty()) {
        return settings.destination_controller_id;
    }
    return settings.controller_id;
}

EcspHeader base_header(
    const application::AgentSettings& settings,
    MessageType type,
    std::uint32_t seq,
    const std::string& dest,
    std::uint64_t timestamp_ms
) {
    EcspHeader header{
        seq,
        settings.ecsp_version,
        settings.ecsp_ver_cap,
        "ap",
        settings.mac,
        type,
        0,
        dest,
        std::nullopt,
    };
    if (timestamp_ms != 0) {
        header.timestamp = timestamp_ms;
    }
    return header;
}

} // namespace

std::string build_discovery_json(
    const application::AgentSettings& settings,
    const domain::AccessPointProfile& profile,
    std::uint32_t seq,
    bool managed_restart,
    std::uint64_t timestamp_ms
) {
    const std::string dest = destination_id(settings);
    std::string body = "{\"deviceInfo\":";
    body += profile.device_info_json(!managed_restart, true);
    body += ",\"deviceMisc\":";
    body += profile.device_misc_json();
    body += ",\"controllerSetting\":";
    body += domain::controller_setting_json(settings.controller_id, dest);
    body += "}";
    return build_message_json(
        base_header(settings, MessageType::Discovery, seq, dest, timestamp_ms),
        body
    );
}

std::string build_preconnect_json(
    const application::AgentSettings& settings,
    const domain::AccessPointProfile& profile,
    std::uint32_t seq,
    const std::string& controller_id,
    bool managed_reconnect,
    std::uint64_t timestamp_ms
) {
    std::string body = "{\"needUsername\":true,\"rebuild\":";
    body += managed_reconnect ? "1" : "0";
    body += ",\"secureCap\":0,\"deviceInfo\":";
    body += profile.device_info_json(!managed_reconnect, true);
    body += ",\"deviceMisc\":";
    body += profile.device_misc_json();
    body += ",\"controllerSetting\":";
    body += domain::controller_setting_json(controller_id);
    body += "}";
    return build_message_json(
        base_header(settings, MessageType::PreConnectInfo, seq, controller_id, timestamp_ms),
        body
    );
}

std::string build_device_verify_json(
    const application::AgentSettings& settings,
    std::uint32_t seq,
    const std::string& controller_id,
    const std::string& auth,
    const std::string& random_key_for_system_verify,
    std::uint64_t timestamp_ms
) {
    std::string body = "{\"auth\":\"";
    body += json_escape(auth);
    body += "\",\"randomKeyForSystemVerify\":\"";
    body += json_escape(random_key_for_system_verify);
    body += "\",\"cipherType\":5}";
    return build_message_json(
        base_header(settings, MessageType::DeviceVerifyInfo, seq, controller_id, timestamp_ms),
        body
    );
}

std::string build_system_verify_result_json(
    const application::AgentSettings& settings,
    std::uint32_t seq,
    const std::string& controller_id,
    std::uint64_t timestamp_ms
) {
    return build_message_json(
        base_header(settings, MessageType::SystemVerifyResult, seq, controller_id, timestamp_ms),
        "{}"
    );
}

std::string build_device_negotiation_json(
    const application::AgentSettings& settings,
    const domain::AccessPointProfile& profile,
    std::uint32_t seq,
    const std::string& controller_id,
    std::uint32_t config_version,
    std::uint64_t timestamp_ms
) {
    std::string body = "{\"configVersion\":";
    body += std::to_string(config_version);
    body += ",\"devCap\":{},\"deviceInfo\":";
    body += profile.adoption_device_info_json();
    body += ",\"controllerSetting\":{\"controllerId\":\"";
    body += json_escape(controller_id);
    body += "\"},\"components\":{},\"components_v2\":";
    body += profile.components_v2_json();
    body += ",\"channelInfo\":";
    body += profile.channel_info_json();
    body += ",\"radioCap\":";
    body += profile.radio_cap_json();
    body += ",\"deviceMisc\":";
    body += profile.device_misc_json();
    body += "}";
    return build_message_json(
        base_header(settings, MessageType::DeviceNegotiation, seq, controller_id, timestamp_ms),
        body
    );
}

std::string build_init_sync_result_json(
    const application::AgentSettings& settings,
    std::uint32_t seq,
    const std::string& controller_id,
    std::uint64_t timestamp_ms
) {
    return build_message_json(
        base_header(settings, MessageType::InitSyncResult, seq, controller_id, timestamp_ms),
        "{}",
        false
    );
}

} // namespace openomada::protocol

