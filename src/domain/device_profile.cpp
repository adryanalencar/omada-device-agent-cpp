#include "openomada/domain/device_profile.hpp"

#include "openomada/protocol/ecsp_message.hpp"

namespace openomada::domain {
namespace {

std::string quote(std::string_view value) {
    std::string out = "\"";
    out += protocol::json_escape(value);
    out += "\"";
    return out;
}

} // namespace

AccessPointProfile::AccessPointProfile(application::AgentSettings settings)
    : settings_(std::move(settings)),
      components_v2_{
          {"system", "2.0"},
          {"configVersion", "1.0"},
          {"time", "1.0"},
          {"informInterval", "1.2"},
          {"devInform", "2.0"},
      } {}

std::string AccessPointProfile::device_info_json(bool is_factory, bool include_ip, bool include_factory_flag) const {
    std::string out = "{";
    bool comma = false;
    const auto add_comma = [&out, &comma]() {
        if (comma) {
            out.push_back(',');
        }
        comma = true;
    };
    if (include_ip) {
        add_comma();
        out += "\"ip\":";
        out += quote(settings_.device_ip);
    }
    if (include_factory_flag) {
        add_comma();
        out += "\"isFactory\":";
        out += is_factory ? "true" : "false";
    }
    add_comma();
    out += "\"name\":";
    out += quote(settings_.device_name);
    add_comma();
    out += "\"model\":";
    out += quote(settings_.model);
    add_comma();
    out += "\"modelVersion\":";
    out += quote(settings_.model_version);
    add_comma();
    out += "\"firmwareVersion\":";
    out += quote(settings_.firmware_version);
    add_comma();
    out += "\"hardwareVersion\":";
    out += quote(settings_.hardware_version);
    add_comma();
    out += "\"upTime\":0,\"cpuUti\":0,\"memUti\":0,\"wirelessLinked\":false,\"p2p\":false,\"supportBridge\":0";
    add_comma();
    out += "\"mainMac\":";
    out += quote(settings_.mac.omada());
    out += "}";
    return out;
}

std::string AccessPointProfile::adoption_device_info_json() const {
    return device_info_json(false, false, false);
}

std::string AccessPointProfile::inform_device_info_json(std::uint64_t uptime_seconds) const {
    std::string out = "{";
    out += "\"ip\":";
    out += quote(settings_.device_ip);
    out += ",\"isFactory\":false";
    out += ",\"name\":";
    out += quote(settings_.device_name);
    out += ",\"model\":";
    out += quote(settings_.model);
    out += ",\"modelVersion\":";
    out += quote(settings_.model_version);
    out += ",\"firmwareVersion\":";
    out += quote(settings_.firmware_version);
    out += ",\"hardwareVersion\":";
    out += quote(settings_.hardware_version);
    out += ",\"upTime\":";
    out += quote(std::to_string(uptime_seconds));
    out += ",\"cpuUti\":0,\"memUti\":0,\"wirelessLinked\":false,\"p2p\":false,\"supportBridge\":0";
    out += ",\"mainMac\":";
    out += quote(settings_.mac.omada());
    out += "}";
    return out;
}

std::string AccessPointProfile::device_misc_json() const {
    std::string out;
    out.reserve(256);
    out += "{\"modelType\":\"NORMAL\",\"support_11ac\":false,\"support_lag\":false,";
    out += "\"supportMesh\":0,\"customizeRegion\":";
    out += std::to_string(settings_.customize_region);
    out += ",\"support_channelLimit\":false,\"channelLimit_mode\":0,\"supportDfs\":0,";
    out += "\"lanPortsNum\":1,\"lanVlanPorts\":[],\"lanPoePorts\":[],\"supportRoaming\":0}";
    return out;
}

std::string AccessPointProfile::components_v2_json() const {
    std::string out = "{";
    bool comma = false;
    for (const auto& component : components_v2_) {
        if (comma) {
            out.push_back(',');
        }
        comma = true;
        out += quote(component.name);
        out.push_back(':');
        out += quote(component.version);
    }
    out += "}";
    return out;
}

std::string AccessPointProfile::channel_info_json() const {
    return "[]";
}

std::string AccessPointProfile::radio_cap_json() const {
    return "[]";
}

std::string controller_setting_json(std::string_view controller_id, std::string_view destination_id) {
    std::string out = "{\"controllerId\":";
    out += quote(controller_id);
    out += ",\"destOmadacId\":";
    out += quote(destination_id.empty() ? controller_id : destination_id);
    out += "}";
    return out;
}

} // namespace openomada::domain
