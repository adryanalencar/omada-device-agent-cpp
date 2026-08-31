#pragma once

#include <string>
#include <vector>

#include "openomada/application/configuration.hpp"
#include "openomada/platform/capabilities.hpp"

namespace openomada::openwrt {

struct UciPlanOptions {
    std::string management_vlan_interface{};
    std::string management_vlan_device{};
};

struct UciValidationResult {
    bool ok{false};
    std::vector<std::string> errors{};
};

struct UciPlan {
    bool ok{false};
    bool changed{false};
    std::vector<std::string> commands{};
    std::vector<std::string> errors{};
    std::string warning{};
};

UciValidationResult validate_update(
    const application::AccessPointConfigUpdate& update,
    const platform::PlatformCapabilities& capabilities,
    const UciPlanOptions& options = {}
);

UciPlan build_uci_plan(
    const application::AccessPointConfigUpdate& update,
    const platform::PlatformCapabilities& capabilities,
    const UciPlanOptions& options = {}
);

std::string render_uci_batch(const std::vector<std::string>& commands);

} // namespace openomada::openwrt
