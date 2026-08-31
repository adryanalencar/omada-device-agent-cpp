#include "openomada/application/configuration_applier.hpp"

#include <utility>

namespace openomada::application {

CompositeConfigurationApplier::CompositeConfigurationApplier(
    std::vector<ConfigurationApplier*> appliers
) : appliers_(std::move(appliers)) {}

void CompositeConfigurationApplier::add(ConfigurationApplier& applier) {
    appliers_.push_back(&applier);
}

ConfigurationApplyResult CompositeConfigurationApplier::apply(
    const AccessPointConfigUpdate& update
) {
    bool changed = false;
    for (ConfigurationApplier* applier : appliers_) {
        if (applier == nullptr) {
            continue;
        }
        const auto result = applier->apply(update);
        if (!result.ok) {
            return result;
        }
        changed = changed || result.changed;
    }
    return {true, changed, {}};
}

} // namespace openomada::application
