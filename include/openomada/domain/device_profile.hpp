#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "openomada/application/settings.hpp"

namespace openomada::domain {

struct ComponentVersion {
    std::string name;
    std::string version;
};

class AccessPointProfile {
public:
    explicit AccessPointProfile(application::AgentSettings settings);

    const application::AgentSettings& settings() const noexcept { return settings_; }

    std::string device_info_json(bool is_factory, bool include_ip = true) const;
    std::string adoption_device_info_json() const;
    std::string device_misc_json() const;
    std::string components_v2_json() const;
    std::string channel_info_json() const;
    std::string radio_cap_json() const;

    const std::vector<ComponentVersion>& components_v2() const noexcept {
        return components_v2_;
    }

private:
    application::AgentSettings settings_;
    std::vector<ComponentVersion> components_v2_;
};

std::string controller_setting_json(std::string_view controller_id, std::string_view destination_id = {});

} // namespace openomada::domain

