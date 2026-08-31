#pragma once

#include <string>
#include <vector>

#include "openomada/application/configuration.hpp"

namespace openomada::application {

struct ConfigurationApplyResult {
    bool ok{false};
    bool changed{false};
    std::string error{};
};

class ConfigurationApplier {
public:
    virtual ~ConfigurationApplier() = default;
    virtual ConfigurationApplyResult apply(const AccessPointConfigUpdate& update) = 0;
};

class CompositeConfigurationApplier final : public ConfigurationApplier {
public:
    explicit CompositeConfigurationApplier(std::vector<ConfigurationApplier*> appliers = {});

    void add(ConfigurationApplier& applier);
    ConfigurationApplyResult apply(const AccessPointConfigUpdate& update) override;

private:
    std::vector<ConfigurationApplier*> appliers_;
};

} // namespace openomada::application
