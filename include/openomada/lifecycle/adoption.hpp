#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "openomada/application/settings.hpp"
#include "openomada/domain/device_profile.hpp"
#include "openomada/transport/frame_transport.hpp"

namespace openomada::lifecycle {

enum class AdoptionError {
    None,
    MissingControllerId,
    SendFailed,
    ReceiveFailed,
    InvalidJson,
    UnexpectedMessage,
    ControllerRejectedPreConnect,
    MissingRandomKey,
    MissingUsername,
    MissingPassword,
    UnsupportedCipher,
    ControllerRejectedDeviceVerify,
    MissingControllerAuth,
    ControllerAuthMismatch,
    ControllerRejectedSystemVerify,
    ControllerRejectedNegotiation,
    ControllerRejectedInitSync,
};

struct AdoptionOptions {
    bool managed_reconnect{false};
    std::optional<std::uint32_t> known_config_version{};
    std::function<std::string()> random_system_key;
    std::uint64_t timestamp_ms{0};
};

struct AdoptionResult {
    bool ok{false};
    bool reached_system_verify{false};
    bool initial_sync_complete{false};
    AdoptionError error{AdoptionError::None};
    std::string detail{};
    std::string controller_id{};
    std::string username{};
    std::optional<std::uint32_t> config_version{};
    std::optional<std::uint32_t> sequence_id{};
};

const char* to_string(AdoptionError error) noexcept;

AdoptionResult run_v2_initial_sync(
    transport::FrameTransport& transport,
    const application::AgentSettings& settings,
    const domain::AccessPointProfile& profile,
    const std::string& controller_id,
    const AdoptionOptions& options = {}
);

} // namespace openomada::lifecycle

