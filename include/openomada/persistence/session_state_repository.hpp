#pragma once

#include <string>

#include "openomada/domain/mac_address.hpp"
#include "openomada/lifecycle/session.hpp"

namespace openomada::persistence {

struct RepositoryStatus {
    bool ok{false};
    std::string error{};

    static RepositoryStatus success();
    static RepositoryStatus failure(std::string message);
};

struct LoadStateResult {
    bool ok{false};
    bool found{false};
    lifecycle::ManagedState state{};
    std::string error{};
};

class JsonSessionStateRepository {
public:
    JsonSessionStateRepository(
        std::string path,
        domain::MacAddress device_mac,
        std::string controller_host
    );

    LoadStateResult load() const;
    RepositoryStatus save(const lifecycle::ManagedState& state) const;
    bool clear() const;

    const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
    domain::MacAddress device_mac_;
    std::string controller_host_;
};

} // namespace openomada::persistence
