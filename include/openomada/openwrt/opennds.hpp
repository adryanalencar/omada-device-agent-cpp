#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openomada/application/client_state.hpp"
#include "openomada/application/configuration_applier.hpp"
#include "openomada/application/configuration.hpp"
#include "openomada/domain/mac_address.hpp"
#include "openomada/platform/capabilities.hpp"

namespace openomada::openwrt {

constexpr const char* kOpenOmadaThemeSpecPath = "/usr/lib/opennds/theme_openomada_redirect.sh";

struct OpenNdsPortalPolicy {
    std::vector<std::string> walled_garden_fqdns{};
    std::vector<std::string> preauthenticated_user_rules{};
    std::vector<std::uint16_t> walled_garden_ports{80, 443, 8088, 8843};
    std::string portal_redirect_url{};
    std::string landing_page_url{};
    std::string default_ssid_name{};
    std::string ap_mac{};
    std::string site_id{};
    std::string site_name{};
};

struct OpenNdsApplyPlan {
    bool ok{false};
    bool changed{false};
    std::vector<std::vector<std::string>> commands{};
    std::string themespec{};
    std::string error{};
};

struct OpenNdsClientParseResult {
    bool ok{false};
    std::vector<application::WirelessClientState> clients{};
    std::string error{};
};

struct OpenNdsCommandResult {
    bool ok{false};
    int exit_code{0};
    std::string output{};
    std::string error{};
};

struct OpenNdsExecutionResult {
    bool ok{false};
    bool changed{false};
    std::size_t command_count{0};
    std::string error{};
};

struct OpenNdsReconcilerOptions {
    std::string controller_host{};
    domain::MacAddress device_mac{};
    std::string site_id{};
    std::string site_name{};
    bool flush_conntrack_on_deauth{true};
};

class OpenNdsExecutor {
public:
    virtual ~OpenNdsExecutor() = default;
    virtual OpenNdsCommandResult run(
        const std::vector<std::string>& command,
        std::string_view input = {}
    ) = 0;
};

class OpenNdsPortalReconciler final : public application::ConfigurationApplier {
public:
    OpenNdsPortalReconciler(
        const platform::PlatformCapabilities& capabilities,
        OpenNdsExecutor& executor,
        OpenNdsReconcilerOptions options
    );

    application::ConfigurationApplyResult apply(const application::AccessPointConfigUpdate& update) override;

private:
    platform::PlatformCapabilities capabilities_;
    OpenNdsExecutor& executor_;
    OpenNdsReconcilerOptions options_;
};

OpenNdsPortalPolicy opennds_portal_policy_from_free_policy(
    const std::optional<application::PortalFreePolicy>& policy
);

OpenNdsPortalPolicy opennds_portal_policy_from_omada_config(
    const application::AccessPointConfigUpdate& update,
    const std::string& controller_host,
    const domain::MacAddress& device_mac,
    const std::string& configured_site_id = {},
    const std::string& configured_site_name = {}
);

OpenNdsApplyPlan build_opennds_apply_plan(const OpenNdsPortalPolicy& policy);
std::string build_openomada_redirect_themespec(const OpenNdsPortalPolicy& policy);
OpenNdsClientParseResult opennds_clients_from_json(std::string_view payload_json) noexcept;
OpenNdsExecutionResult execute_opennds_apply_plan(
    const OpenNdsApplyPlan& plan,
    OpenNdsExecutor& executor
);

} // namespace openomada::openwrt
