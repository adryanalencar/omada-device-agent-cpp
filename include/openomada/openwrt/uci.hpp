#pragma once

#include <string>
#include <vector>

#include "openomada/application/configuration_applier.hpp"
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

struct UciExecutionResult {
    bool ok{false};
    bool changed{false};
    std::string error{};
    std::size_t command_count{0};
};

class UciExecutor {
public:
    virtual ~UciExecutor() = default;
    virtual UciExecutionResult apply_batch(const std::string& batch) = 0;
    virtual UciExecutionResult reload_wifi() = 0;
};

class OpenWrtUciReconciler final : public application::ConfigurationApplier {
public:
    OpenWrtUciReconciler(
        const platform::PlatformCapabilities& capabilities,
        UciExecutor& executor,
        UciPlanOptions options = {}
    );

    application::ConfigurationApplyResult apply(const application::AccessPointConfigUpdate& update) override;

private:
    platform::PlatformCapabilities capabilities_;
    UciExecutor& executor_;
    UciPlanOptions options_;
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
