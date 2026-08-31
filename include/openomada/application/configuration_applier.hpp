#pragma once

#include <string>

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

} // namespace openomada::application
