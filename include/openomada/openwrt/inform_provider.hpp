#pragma once

#include <string>

#include "openomada/application/inform.hpp"
#include "openomada/openwrt/opennds.hpp"

namespace openomada::openwrt {

struct OpenWrtInformProviderOptions {
    std::string dhcp_leases_path{"/tmp/dhcp.leases"};
    bool include_opennds{true};
};

class OpenWrtInformProvider final : public application::InformProvider {
public:
    OpenWrtInformProvider(OpenNdsExecutor& executor, OpenWrtInformProviderOptions options = {});

    application::InformSnapshot build(bool need_reply, std::uint64_t uptime_seconds) override;

private:
    OpenNdsExecutor& executor_;
    OpenWrtInformProviderOptions options_;
};

} // namespace openomada::openwrt
