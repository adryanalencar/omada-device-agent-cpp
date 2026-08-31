#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "openomada/application/configuration.hpp"
#include "openomada/application/configuration_applier.hpp"
#include "openomada/application/daemon_config.hpp"
#include "openomada/application/inform.hpp"
#include "openomada/domain/device_profile.hpp"
#include "openomada/lifecycle/adoption.hpp"
#include "openomada/lifecycle/discovery.hpp"
#include "openomada/lifecycle/managed_request_handler.hpp"
#include "openomada/lifecycle/managed_session.hpp"
#include "openomada/lifecycle/reconnect.hpp"
#include "openomada/openwrt/inform_provider.hpp"
#include "openomada/openwrt/opennds.hpp"
#include "openomada/openwrt/process.hpp"
#include "openomada/openwrt/uci.hpp"
#include "openomada/persistence/session_state_repository.hpp"
#include "openomada/platform/capabilities.hpp"
#include "openomada/transport/tls_client.hpp"
#include "openomada/transport/udp_discovery.hpp"

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

void request_stop(int /*signal*/) {
    g_stop_requested = 1;
}

bool stop_requested() noexcept {
    return g_stop_requested != 0;
}

enum class LogLevel : std::uint8_t {
    Error = 0,
    Warning = 1,
    Info = 2,
    Debug = 3,
};

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

LogLevel parse_log_level(std::string value) {
    value = lowercase(std::move(value));
    if (value == "debug" || value == "trace") {
        return LogLevel::Debug;
    }
    if (value == "warn" || value == "warning") {
        return LogLevel::Warning;
    }
    if (value == "error") {
        return LogLevel::Error;
    }
    return LogLevel::Info;
}

class Logger {
public:
    explicit Logger(LogLevel level) : level_(level) {}

    void error(std::string message) const { write(LogLevel::Error, "error", std::move(message)); }
    void warning(std::string message) const { write(LogLevel::Warning, "warning", std::move(message)); }
    void info(std::string message) const { write(LogLevel::Info, "info", std::move(message)); }
    void debug(std::string message) const { write(LogLevel::Debug, "debug", std::move(message)); }

private:
    bool enabled(LogLevel level) const noexcept {
        return static_cast<std::uint8_t>(level) <= static_cast<std::uint8_t>(level_);
    }

    void write(LogLevel level, const char* label, const std::string& message) const {
        if (!enabled(level)) {
            return;
        }
        std::cerr << "openomada-agent-native[" << label << "]: " << message << '\n';
    }

    LogLevel level_{LogLevel::Info};
};

std::uint64_t now_ms() noexcept {
    timespec ts {};
    if (::clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }
    const std::uint64_t seconds = ts.tv_sec < 0 ? 0ULL : static_cast<std::uint64_t>(ts.tv_sec);
    const std::uint64_t nanos = ts.tv_nsec < 0 ? 0ULL : static_cast<std::uint64_t>(ts.tv_nsec);
    return seconds * 1000ULL + nanos / 1000000ULL;
}

void sleep_ms(std::uint32_t milliseconds) noexcept {
    timespec remaining {};
    remaining.tv_sec = static_cast<time_t>(milliseconds / 1000U);
    remaining.tv_nsec = static_cast<long>((milliseconds % 1000U) * 1000000UL);
    while (!stop_requested() && ::nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) {
            break;
        }
    }
}

std::optional<std::string> getenv_string(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
}

std::vector<openomada::application::EnvironmentVariable> collect_environment() {
    constexpr const char* kNames[] = {
        "OPENOMADA_CONFIG",
        "OPENOMADA_ENABLED",
        "OPENOMADA_CONTROLLER_HOST",
        "OPENOMADA_CONTROLLER_ID",
        "OPENOMADA_SITE_ID",
        "OPENOMADA_DEVICE_USERNAME",
        "OPENOMADA_DEVICE_PASSWORD",
        "OPENOMADA_DEVICE_ACCOUNT_PASSWORD",
        "OPENOMADA_STATE_PATH",
        "OPENOMADA_LOG_LEVEL",
        "OPENOMADA_PLATFORM",
        "OPENOMADA_RADIO_BANDS",
        "OPENOMADA_PROTOCOL_TRACE",
        "OPENOMADA_INFORM_INTERVAL_MS",
        "OPENOMADA_TCP_TIMEOUT_SECONDS",
        "OPENOMADA_RECONNECT_DELAY_MS",
        "OPENOMADA_MANAGED_RECONNECT_ATTEMPTS",
        "OPENOMADA_MAX_SSIDS",
        "OPENOMADA_DISCOVERY_PORT",
        "OPENOMADA_LOCAL_DISCOVERY_PORT",
        "OPENOMADA_MANAGE_PORT",
        "OPENOMADA_TLS_VERIFY",
        "OPENOMADA_TLS_CA_FILE",
        "OPENOMADA_DEVICE_NAME",
        "OPENOMADA_MODEL",
        "OPENOMADA_MODEL_VERSION",
        "OPENOMADA_HARDWARE_VERSION",
        "OPENOMADA_FIRMWARE_VERSION",
        "OPENOMADA_DEVICE_IP",
        "OPENOMADA_CUSTOMIZE_REGION",
        "OPENOMADA_DEVICE_MAC",
        "OPENOMADA_MANAGEMENT_VLAN_INTERFACE",
        "OPENOMADA_MANAGEMENT_VLAN_DEVICE",
        "OPENOMADA_PORTAL_ENGINE",
        "OPENOMADA_PORTAL_ENABLED",
        "OPENOMADA_FLUSH_CONNTRACK_ON_DEAUTH",
        "OPENOMADA_DESTINATION_CONTROLLER_ID",
    };

    std::vector<openomada::application::EnvironmentVariable> environment;
    for (const char* name : kNames) {
        if (auto value = getenv_string(name)) {
            environment.push_back({name, *value});
        }
    }
    return environment;
}

void apply_environment_config_path(openomada::application::RuntimeOptions* options) {
    if (options == nullptr) {
        return;
    }
    auto env_config = getenv_string("OPENOMADA_CONFIG");
    if (env_config.has_value() && !env_config->empty() &&
        options->config_path == "/etc/config/openomada") {
        options->config_path = *env_config;
    }
}

void print_help() {
    std::cout
        << "Usage: openomada-agent-native [--config PATH] [--check-config] [--dry-run] [--once]\n"
        << "\n"
        << "Options:\n"
        << "  --config PATH      UCI-style runtime config path, default /etc/config/openomada\n"
        << "  --check-config     Load and validate config, then exit\n"
        << "  --dry-run          Detect capabilities and print runtime plan without network I/O\n"
        << "  --once             Stop after adoption/reconnect and initial sync\n"
        << "  --version          Print version and exit\n"
        << "  --help             Print this help\n";
}

std::string host_port(const std::string& host, std::uint16_t port) {
    return host + ":" + std::to_string(port);
}

class RuntimeSupportGuard final : public openomada::application::ConfigurationApplier {
public:
    explicit RuntimeSupportGuard(bool portal_supported) : portal_supported_(portal_supported) {}

    openomada::application::ConfigurationApplyResult apply(
        const openomada::application::AccessPointConfigUpdate& update
    ) override {
        if (update.led.has_value() || update.wifi_control_led.has_value()) {
            return {false, false, "LED configuration is parsed but has no native OpenWrt backend yet"};
        }
        if (!update.client_operations.empty()) {
            return {false, false, "clientOperation is parsed but has no native OpenWrt backend yet"};
        }
        if (update.client_rate_config.has_value()) {
            return {false, false, "clientRateConfig is parsed but has no native OpenWrt backend yet"};
        }
        if (!portal_supported_ &&
            (update.portal_free_policy.has_value() ||
             !update.portal_configs.empty() ||
             !update.client_configs.empty())) {
            return {false, false, "portal configuration requires enabled openNDS capability"};
        }
        return {true, false, {}};
    }

private:
    bool portal_supported_{false};
};

openomada::platform::PlatformCapabilities detect_runtime_capabilities(
    const openomada::application::DaemonConfig& config,
    const Logger& logger
) {
    openomada::platform::PlatformCapabilityInput input;
    input.platform = config.openwrt.platform;
    input.radio_bands = config.openwrt.radio_bands;
    input.max_ssids = config.openwrt.max_ssids;
    input.tools.uci = openomada::openwrt::command_available("uci");
    input.tools.ubus = openomada::openwrt::command_available("ubus");
    input.tools.nft = openomada::openwrt::command_available("nft");
    input.tools.hostapd = openomada::openwrt::command_available("hostapd");
    input.tools.dnsmasq = openomada::openwrt::command_available("dnsmasq");
    input.tools.opennds = openomada::openwrt::command_available("opennds");
    input.tools.ndsctl = openomada::openwrt::command_available("ndsctl");
    input.tools.iw = openomada::openwrt::command_available("iw");

    if (input.tools.iw) {
        const auto iw = openomada::openwrt::run_process({"iw", "list"});
        if (iw.ok) {
            input.iw_list_output = iw.output;
        } else {
            logger.debug("could not read iw list: " + iw.error);
        }
    }

    input.cap_ssid_vlan = input.tools.uci;
    input.cap_management_vlan =
        input.tools.uci &&
        !config.openwrt.management_vlan_interface.empty() &&
        !config.openwrt.management_vlan_device.empty();
    input.cap_portal =
        config.openwrt.portal_enabled &&
        lowercase(config.openwrt.portal_engine) == "opennds" &&
        input.tools.opennds &&
        input.tools.ndsctl;
    input.cap_client_operations = false;
    input.cap_client_rate_limits = false;
    input.cap_led = false;

    auto result = openomada::platform::detect_platform_capabilities(input);
    if (!result.ok) {
        logger.warning("capability detection failed: " + result.error + "; using generic disabled capabilities");
        return {};
    }
    return result.capabilities;
}

struct LiveSession {
    std::unique_ptr<openomada::transport::TlsFrameTransport> transport{};
    openomada::application::AgentSettings settings{};
    openomada::lifecycle::ManagedState state{};
    openomada::lifecycle::AdoptionResult adoption{};
};

struct LiveSessionResult {
    bool ok{false};
    LiveSession session{};
    std::string error{};
};

LiveSessionResult connect_and_sync(
    const openomada::application::AgentSettings& settings,
    std::uint16_t manage_port,
    const std::string& controller_id,
    const openomada::lifecycle::AdoptionOptions& adoption_options,
    const Logger& logger
) {
    LiveSessionResult result;
    if (controller_id.empty()) {
        result.error = "controller id is empty";
        return result;
    }

    auto managed_settings = settings;
    managed_settings.controller_id = controller_id;
    managed_settings.manage_port = manage_port;
    openomada::domain::AccessPointProfile profile(managed_settings);

    auto transport = std::make_unique<openomada::transport::TlsFrameTransport>();
    logger.info("connecting management TLS " + host_port(managed_settings.controller_host, manage_port));
    const auto connected = transport->connect({
        managed_settings.controller_host,
        manage_port,
        managed_settings.tcp_timeout_seconds,
        managed_settings.tls_verify,
        managed_settings.tls_ca_file,
    });
    if (!connected.ok) {
        result.error = connected.error;
        return result;
    }

    auto options = adoption_options;
    options.timestamp_ms = options.timestamp_ms == 0 ? now_ms() : options.timestamp_ms;
    auto adoption = openomada::lifecycle::run_v2_initial_sync(
        *transport,
        managed_settings,
        profile,
        controller_id,
        options
    );
    if (!adoption.ok || !adoption.initial_sync_complete) {
        std::string error = openomada::lifecycle::to_string(adoption.error);
        if (!adoption.detail.empty()) {
            error += ": ";
            error += adoption.detail;
        }
        result.error = error;
        return result;
    }

    result.ok = true;
    result.session.transport = std::move(transport);
    result.session.settings = managed_settings;
    result.session.adoption = adoption;
    result.session.state = openomada::lifecycle::managed_state_from_adoption(
        managed_settings,
        manage_port,
        adoption,
        now_ms()
    );
    return result;
}

void save_state(
    const openomada::persistence::SessionStateRepository& repository,
    const openomada::lifecycle::ManagedState& state,
    const Logger& logger
) {
    const auto saved = repository.save(state);
    if (!saved.ok) {
        logger.warning("could not persist managed state: " + saved.error);
    }
}

void send_managed_rediscovery(
    const openomada::application::AgentSettings& settings,
    const openomada::lifecycle::ManagedState& state,
    const Logger& logger
) {
    auto managed_settings = openomada::lifecycle::settings_for_managed_state(settings, state);
    openomada::domain::AccessPointProfile profile(managed_settings);
    openomada::transport::UdpDiscoveryTransport udp;
    const auto opened = udp.open({managed_settings.local_discovery_port, 1});
    if (!opened.ok) {
        logger.warning("managed rediscovery UDP open failed: " + opened.error);
        return;
    }
    const auto sent = openomada::lifecycle::send_discovery_once(
        udp,
        {state.controller_host, managed_settings.discovery_port},
        managed_settings,
        profile,
        1,
        true,
        now_ms()
    );
    if (!sent.ok) {
        logger.warning("managed rediscovery send failed: " + sent.error);
    } else {
        logger.info("managed rediscovery sent to " + host_port(state.controller_host, managed_settings.discovery_port));
    }
}

LiveSessionResult try_managed_reconnect(
    const openomada::persistence::SessionStateRepository& repository,
    const openomada::application::AgentSettings& settings,
    const Logger& logger
) {
    LiveSessionResult result;
    const auto loaded = repository.load();
    if (!loaded.ok) {
        logger.warning("managed state load failed: " + loaded.error);
        result.error = loaded.error;
        return result;
    }
    if (!loaded.found) {
        if (!loaded.error.empty()) {
            logger.debug("managed state ignored: " + loaded.error);
        }
        result.error = "no managed state";
        return result;
    }

    for (std::uint32_t attempt = 0; attempt < settings.managed_reconnect_attempts && !stop_requested(); ++attempt) {
        auto managed_settings = openomada::lifecycle::settings_for_managed_state(settings, loaded.state);
        auto options = openomada::lifecycle::adoption_options_for_managed_state(loaded.state);
        logger.info(
            "managed reconnect attempt " +
            std::to_string(attempt + 1U) +
            "/" +
            std::to_string(settings.managed_reconnect_attempts)
        );
        auto session = connect_and_sync(
            managed_settings,
            loaded.state.manage_port,
            loaded.state.controller_id,
            options,
            logger
        );
        if (session.ok) {
            logger.info("managed reconnect succeeded");
            return session;
        }
        result.error = session.error;
        logger.warning("managed reconnect failed: " + session.error);
        sleep_ms(settings.reconnect_delay_ms);
    }

    if (!stop_requested()) {
        send_managed_rediscovery(settings, loaded.state, logger);
    }
    return result;
}

LiveSessionResult discover_and_adopt(
    const openomada::application::AgentSettings& settings,
    const Logger& logger
) {
    LiveSessionResult result;
    openomada::transport::UdpDiscoveryTransport udp;
    const auto opened = udp.open({settings.local_discovery_port, 2});
    if (!opened.ok) {
        result.error = opened.error;
        return result;
    }

    auto discovery_settings = settings;
    openomada::domain::AccessPointProfile profile(discovery_settings);
    const openomada::transport::UdpEndpoint endpoint{
        discovery_settings.controller_host,
        discovery_settings.discovery_port,
    };

    std::uint32_t seq = 1;
    bool announced = false;
    while (!stop_requested()) {
        if (!announced) {
            logger.info("discovering controller at " + host_port(endpoint.host, endpoint.port));
            announced = true;
        }
        const auto sent = openomada::lifecycle::send_discovery_once(
            udp,
            endpoint,
            discovery_settings,
            profile,
            seq++,
            false,
            now_ms()
        );
        if (!sent.ok) {
            result.error = sent.error;
            logger.warning("discovery send failed: " + sent.error);
            sleep_ms(discovery_settings.reconnect_delay_ms);
            continue;
        }

        while (!stop_requested()) {
            auto received = udp.receive_payload();
            if (!received.ok) {
                if (received.timeout) {
                    logger.debug("discovery receive timeout");
                    break;
                }
                logger.warning("discovery receive failed: " + received.error);
                break;
            }
            auto pre_adopt = openomada::lifecycle::parse_pre_adopt_request(
                received.payload,
                discovery_settings.controller_id,
                discovery_settings.manage_port
            );
            if (!pre_adopt.ok) {
                logger.debug("ignored malformed discovery response: " + pre_adopt.error);
                continue;
            }
            if (!pre_adopt.is_pre_adopt) {
                logger.debug("ignored non PRE_ADOPT discovery response");
                continue;
            }
            if (pre_adopt.controller_id.empty()) {
                logger.warning("PRE_ADOPT_REQUEST did not include a usable controller id");
                continue;
            }

            auto adopt_settings = discovery_settings;
            adopt_settings.controller_id = pre_adopt.controller_id;
            adopt_settings.destination_controller_id = pre_adopt.destination_id;
            openomada::lifecycle::AdoptionOptions adoption_options;
            auto session = connect_and_sync(
                adopt_settings,
                pre_adopt.adopt_port,
                pre_adopt.controller_id,
                adoption_options,
                logger
            );
            if (session.ok) {
                logger.info("adoption and initial sync completed");
                return session;
            }
            result.error = session.error;
            logger.warning("adoption failed: " + session.error);
            break;
        }
        sleep_ms(discovery_settings.reconnect_delay_ms);
    }

    if (result.error.empty()) {
        result.error = "stopped";
    }
    return result;
}

enum class ManagedLoopOutcome {
    Stopped,
    Reconnect,
};

ManagedLoopOutcome run_managed_loop(
    LiveSession* session,
    const openomada::persistence::SessionStateRepository& repository,
    openomada::application::ConfigurationApplier& applier,
    openomada::application::InformProvider& inform_provider,
    const Logger& logger
) {
    if (session == nullptr || session->transport == nullptr) {
        return ManagedLoopOutcome::Reconnect;
    }

    openomada::domain::AccessPointProfile profile(session->settings);
    openomada::lifecycle::InformScheduler scheduler(session->settings.inform_interval_ms);
    scheduler.start(session->adoption.last_seq, now_ms());
    logger.info("entered managed session");

    while (!stop_requested()) {
        const auto scheduled = scheduler.poll(now_ms());
        if (scheduled.due) {
            const auto sent = openomada::lifecycle::send_inform(
                *session->transport,
                session->settings,
                profile,
                inform_provider,
                session->state.controller_id,
                scheduled,
                now_ms()
            );
            if (!sent.ok) {
                logger.warning("INFORM_REQUEST send failed: " + sent.error);
                return ManagedLoopOutcome::Reconnect;
            }
            logger.debug(
                "sent INFORM_REQUEST seq=" +
                std::to_string(scheduled.seq) +
                " needReply=" +
                std::to_string(scheduled.need_reply ? 1 : 0)
            );
        }

        auto received = session->transport->receive_payload();
        if (!received.ok) {
            if (received.timeout) {
                continue;
            }
            logger.warning("managed receive failed: " + received.error);
            return ManagedLoopOutcome::Reconnect;
        }

        auto result = openomada::lifecycle::handle_managed_request(
            *session->transport,
            session->settings,
            &session->state,
            received.payload,
            &applier,
            now_ms()
        );
        if (!result.ok) {
            logger.warning("managed request failed: " + result.error);
            continue;
        }
        if (result.action != openomada::lifecycle::ManagedRequestAction::Ignored) {
            logger.info(std::string("managed action: ") + openomada::lifecycle::to_string(result.action));
        }
        if (!result.error.empty()) {
            logger.warning("managed action detail: " + result.error);
        }

        if (result.action == openomada::lifecycle::ManagedRequestAction::SetResponse &&
            (result.config_version.has_value() || result.sequence_id.has_value())) {
            session->state.updated_at = now_ms();
            save_state(repository, session->state, logger);
        }
        if (result.should_clear_state) {
            if (!repository.clear()) {
                logger.warning("managed state clear failed");
            }
        }
        if (result.should_end_session) {
            logger.info("managed session ended by controller request");
            return ManagedLoopOutcome::Reconnect;
        }
    }
    return ManagedLoopOutcome::Stopped;
}

int run_daemon(
    const openomada::application::RuntimeOptions& options,
    const openomada::application::DaemonConfig& config
) {
    const Logger logger(parse_log_level(config.log_level));
    if (!config.enabled) {
        logger.info("agent disabled by config: " + options.config_path);
        return 0;
    }

    logger.info("config: " + openomada::application::daemon_config_summary(config));
    openomada::openwrt::OpenWrtProcessExecutor executor;
    const auto capabilities = detect_runtime_capabilities(config, logger);
    logger.info("capabilities: " + openomada::platform::capability_summary(capabilities));
    if (options.dry_run) {
        logger.info("dry-run requested; no discovery or management connection will be opened");
        return 0;
    }

    openomada::persistence::JsonSessionStateRepository repository(
        config.settings.state_file,
        config.settings.mac,
        config.settings.controller_host
    );

    openomada::openwrt::UciPlanOptions uci_options;
    uci_options.management_vlan_interface = config.openwrt.management_vlan_interface;
    uci_options.management_vlan_device = config.openwrt.management_vlan_device;
    openomada::openwrt::OpenWrtUciReconciler uci_reconciler(capabilities, executor, uci_options);

    openomada::openwrt::OpenNdsReconcilerOptions opennds_options;
    opennds_options.controller_host = config.settings.controller_host;
    opennds_options.device_mac = config.settings.mac;
    opennds_options.site_id = config.settings.site_id;
    opennds_options.flush_conntrack_on_deauth = config.openwrt.flush_conntrack_on_deauth;
    openomada::openwrt::OpenNdsPortalReconciler opennds_reconciler(
        capabilities,
        executor,
        opennds_options
    );

    RuntimeSupportGuard support_guard(capabilities.supports_portal);
    openomada::application::CompositeConfigurationApplier appliers;
    appliers.add(support_guard);
    appliers.add(uci_reconciler);
    if (capabilities.supports_portal) {
        appliers.add(opennds_reconciler);
    }

    openomada::openwrt::OpenWrtInformProviderOptions inform_options;
    inform_options.include_opennds = capabilities.supports_portal;
    openomada::openwrt::OpenWrtInformProvider live_inform_provider(executor, inform_options);
    openomada::application::StaticInformProvider static_inform_provider;
    openomada::application::InformProvider* inform_provider =
        capabilities.tools.ubus ? static_cast<openomada::application::InformProvider*>(&live_inform_provider)
                                : static_cast<openomada::application::InformProvider*>(&static_inform_provider);

    while (!stop_requested()) {
        auto session = try_managed_reconnect(repository, config.settings, logger);
        if (!session.ok && !stop_requested()) {
            session = discover_and_adopt(config.settings, logger);
        }
        if (!session.ok) {
            if (stop_requested()) {
                break;
            }
            logger.warning("could not establish managed session: " + session.error);
            sleep_ms(config.settings.reconnect_delay_ms);
            continue;
        }

        save_state(repository, session.session.state, logger);
        if (options.once) {
            logger.info("--once requested; exiting after initial sync");
            return 0;
        }

        const auto outcome = run_managed_loop(
            &session.session,
            repository,
            appliers,
            *inform_provider,
            logger
        );
        if (outcome == ManagedLoopOutcome::Stopped) {
            break;
        }
        if (!stop_requested()) {
            sleep_ms(config.settings.reconnect_delay_ms);
        }
    }

    logger.info("stopped");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);

    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        args.emplace_back(argv[index] == nullptr ? "" : argv[index]);
    }

    auto parsed_options = openomada::application::parse_runtime_options(args);
    if (!parsed_options.ok) {
        std::cerr << "openomada-agent-native[error]: " << parsed_options.error << '\n';
        return 2;
    }
    if (parsed_options.show_help) {
        print_help();
        return 0;
    }
    if (parsed_options.show_version) {
        std::cout << "openomada-agent-native 0.1.0\n";
        return 0;
    }

    apply_environment_config_path(&parsed_options.options);
    const auto environment = collect_environment();
    auto loaded = openomada::application::load_daemon_config_file(
        parsed_options.options,
        environment
    );
    if (!loaded.ok) {
        std::cerr << "openomada-agent-native[error]: " << loaded.error << '\n';
        return 1;
    }
    if (parsed_options.options.check_config) {
        std::cout << openomada::application::daemon_config_summary(loaded.config) << '\n';
        return 0;
    }

    return run_daemon(parsed_options.options, loaded.config);
}
