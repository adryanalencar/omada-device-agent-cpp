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

class SessionStateRepository {
public:
    virtual ~SessionStateRepository() = default;
    virtual LoadStateResult load() const = 0;
    virtual RepositoryStatus save(const lifecycle::ManagedState& state) const = 0;
    virtual bool clear() const = 0;
};

class JsonSessionStateRepository final : public SessionStateRepository {
public:
    JsonSessionStateRepository(
        std::string path,
        domain::MacAddress device_mac,
        std::string controller_host
    );

    LoadStateResult load() const override;
    RepositoryStatus save(const lifecycle::ManagedState& state) const override;
    bool clear() const override;

    const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
    domain::MacAddress device_mac_;
    std::string controller_host_;
};

} // namespace openomada::persistence
