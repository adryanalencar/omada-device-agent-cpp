#include "openomada/lifecycle/adoption.hpp"

#include "openomada/crypto/ecsp_auth.hpp"
#include "openomada/crypto/random.hpp"
#include "openomada/protocol/ecsp_builders.hpp"
#include "openomada/protocol/json.hpp"
#include "openomada/protocol/message_type.hpp"

#include <cctype>
#include <utility>

namespace openomada::lifecycle {
namespace {

AdoptionResult failure(AdoptionError error, std::string detail, const std::string& controller_id, const std::string& username = {}) {
    AdoptionResult result;
    result.error = error;
    result.detail = std::move(detail);
    result.controller_id = controller_id;
    result.username = username;
    return result;
}

struct ReceivedMessage {
    bool ok{false};
    AdoptionError error{AdoptionError::None};
    std::string detail{};
    protocol::JsonDocument document{};
};

ReceivedMessage receive_until(
    transport::FrameTransport& transport,
    protocol::MessageType expected,
    std::uint32_t max_skipped_messages = 8
) {
    for (std::uint32_t skipped = 0; skipped <= max_skipped_messages; ++skipped) {
        auto received = transport.receive_payload();
        if (!received.ok) {
            return {false, AdoptionError::ReceiveFailed, received.error, {}};
        }
        auto document = protocol::JsonDocument::parse(received.payload);
        if (!document.valid()) {
            return {false, AdoptionError::InvalidJson, "controller sent invalid ECSP JSON", {}};
        }
        auto type = protocol::ecsp_header_type(document.get());
        if (!type.has_value()) {
            return {false, AdoptionError::InvalidJson, "controller message has no header.type", {}};
        }
        if (*type == protocol::to_underlying(expected)) {
            return {true, AdoptionError::None, {}, std::move(document)};
        }
    }
    return {false, AdoptionError::UnexpectedMessage, "expected ECSP message was not received", {}};
}

bool send_or_fail(
    transport::FrameTransport& transport,
    std::string_view payload,
    std::uint32_t seq,
    AdoptionResult* result
) {
    auto status = transport.send_payload(payload);
    if (status.ok) {
        if (result != nullptr) {
            result->last_seq = seq;
        }
        return true;
    }
    if (result != nullptr) {
        result->error = AdoptionError::SendFailed;
        result->detail = status.error;
    }
    return false;
}

std::optional<std::uint32_t> uint_body_field(json_object* body, const char* name) {
    auto value = protocol::json_int(protocol::object_member(body, name));
    if (!value.has_value() || *value < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

std::string ascii_upper(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

} // namespace

const char* to_string(AdoptionError error) noexcept {
    switch (error) {
    case AdoptionError::None:
        return "none";
    case AdoptionError::MissingControllerId:
        return "missing-controller-id";
    case AdoptionError::SendFailed:
        return "send-failed";
    case AdoptionError::ReceiveFailed:
        return "receive-failed";
    case AdoptionError::InvalidJson:
        return "invalid-json";
    case AdoptionError::UnexpectedMessage:
        return "unexpected-message";
    case AdoptionError::ControllerRejectedPreConnect:
        return "controller-rejected-preconnect";
    case AdoptionError::MissingRandomKey:
        return "missing-random-key";
    case AdoptionError::MissingUsername:
        return "missing-username";
    case AdoptionError::MissingPassword:
        return "missing-password";
    case AdoptionError::UnsupportedCipher:
        return "unsupported-cipher";
    case AdoptionError::ControllerRejectedDeviceVerify:
        return "controller-rejected-device-verify";
    case AdoptionError::MissingControllerAuth:
        return "missing-controller-auth";
    case AdoptionError::ControllerAuthMismatch:
        return "controller-auth-mismatch";
    case AdoptionError::ControllerRejectedSystemVerify:
        return "controller-rejected-system-verify";
    case AdoptionError::ControllerRejectedNegotiation:
        return "controller-rejected-negotiation";
    case AdoptionError::ControllerRejectedInitSync:
        return "controller-rejected-init-sync";
    }
    return "unknown";
}

AdoptionResult run_v2_initial_sync(
    transport::FrameTransport& transport,
    const application::AgentSettings& settings,
    const domain::AccessPointProfile& profile,
    const std::string& controller_id,
    const AdoptionOptions& options
) {
    if (controller_id.empty()) {
        return failure(
            AdoptionError::MissingControllerId,
            "PRE_ADOPT_REQUEST did not provide header.dest and controller_id is empty",
            controller_id
        );
    }

    AdoptionResult result;
    result.controller_id = controller_id;

    std::uint32_t seq = 1;
    if (!send_or_fail(
            transport,
            protocol::build_preconnect_json(
                settings,
                profile,
                seq,
                controller_id,
                options.managed_reconnect,
                options.timestamp_ms
            ),
            seq,
            &result
        )) {
        return result;
    }

    auto preconnect_response = receive_until(
        transport,
        protocol::MessageType::PreConnectInfoResponse
    );
    if (!preconnect_response.ok) {
        return failure(preconnect_response.error, preconnect_response.detail, controller_id);
    }
    if (protocol::ecsp_header_error(preconnect_response.document.get()) != 0) {
        return failure(AdoptionError::ControllerRejectedPreConnect, "controller rejected PRE_CONNECT_INFO", controller_id);
    }

    auto* preconnect_body = protocol::ecsp_body(preconnect_response.document.get());
    auto random_device_key = protocol::json_string(
        protocol::object_member(preconnect_body, "randomKeyForDeviceVerify")
    );
    if (!random_device_key.has_value() || random_device_key->size() < 36) {
        return failure(AdoptionError::MissingRandomKey, "PRE_CONNECT_INFO_RESPONSE has no usable randomKeyForDeviceVerify", controller_id);
    }

    std::string username = settings.device_username;
    if (username.empty()) {
        auto response_username = protocol::json_string(protocol::object_member(preconnect_body, "username"));
        if (response_username.has_value()) {
            username = *response_username;
        }
    }
    if (username.empty()) {
        return failure(AdoptionError::MissingUsername, "controller did not return a Device Account username", controller_id);
    }
    result.username = username;

    if (settings.device_password.empty()) {
        return failure(AdoptionError::MissingPassword, "Device Account password is empty", controller_id, username);
    }
    if (settings.device_cipher_type != 5) {
        return failure(AdoptionError::UnsupportedCipher, "only ECSP V2 cipherType=5 is implemented", controller_id, username);
    }

    std::string random_system_key = options.random_system_key ? options.random_system_key() : crypto::random_uuid_v4();
    if (random_system_key.empty()) {
        return failure(AdoptionError::MissingRandomKey, "could not generate randomKeyForSystemVerify", controller_id, username);
    }

    const std::string device_auth = crypto::calculate_md5_mode_auth(
        username,
        settings.device_password,
        *random_device_key
    );
    ++seq;
    if (!send_or_fail(
            transport,
            protocol::build_device_verify_json(
                settings,
                seq,
                controller_id,
                device_auth,
                random_system_key,
                options.timestamp_ms
            ),
            seq,
            &result
        )) {
        return result;
    }

    auto device_verify_response = receive_until(
        transport,
        protocol::MessageType::DeviceVerifyResponse
    );
    if (!device_verify_response.ok) {
        return failure(device_verify_response.error, device_verify_response.detail, controller_id, username);
    }
    if (protocol::ecsp_header_error(device_verify_response.document.get()) != 0) {
        return failure(AdoptionError::ControllerRejectedDeviceVerify, "controller rejected DEVICE_VERIFY_INFO", controller_id, username);
    }
    auto* verify_body = protocol::ecsp_body(device_verify_response.document.get());
    auto controller_auth = protocol::json_string(protocol::object_member(verify_body, "auth"));
    if (!controller_auth.has_value()) {
        return failure(AdoptionError::MissingControllerAuth, "DEVICE_VERIFY_RESPONSE has no auth field", controller_id, username);
    }
    const std::string expected_controller_auth = crypto::calculate_md5_mode_auth(
        username,
        settings.device_password,
        random_system_key
    );
    if (ascii_upper(*controller_auth) != ascii_upper(expected_controller_auth)) {
        return failure(AdoptionError::ControllerAuthMismatch, "controller system-auth did not verify", controller_id, username);
    }

    ++seq;
    if (!send_or_fail(
            transport,
            protocol::build_system_verify_result_json(settings, seq, controller_id, options.timestamp_ms),
            seq,
            &result
        )) {
        return result;
    }

    auto verify_ack = receive_until(transport, protocol::MessageType::VerifyResultAck);
    if (!verify_ack.ok) {
        return failure(verify_ack.error, verify_ack.detail, controller_id, username);
    }
    if (protocol::ecsp_header_error(verify_ack.document.get()) != 0) {
        return failure(AdoptionError::ControllerRejectedSystemVerify, "controller rejected SYSTEM_VERIFY_RESULT", controller_id, username);
    }
    result.reached_system_verify = true;

    const std::uint32_t negotiation_config_version =
        options.managed_reconnect && options.known_config_version.has_value()
            ? *options.known_config_version
            : 0;
    ++seq;
    if (!send_or_fail(
            transport,
            protocol::build_device_negotiation_json(
                settings,
                profile,
                seq,
                controller_id,
                negotiation_config_version,
                options.timestamp_ms
            ),
            seq,
            &result
        )) {
        return result;
    }

    auto system_negotiation = receive_until(transport, protocol::MessageType::SystemNegotiation);
    if (!system_negotiation.ok) {
        return failure(system_negotiation.error, system_negotiation.detail, controller_id, username);
    }
    if (protocol::ecsp_header_error(system_negotiation.document.get()) != 0) {
        return failure(AdoptionError::ControllerRejectedNegotiation, "controller rejected DEVICE_NEGOTIATION", controller_id, username);
    }
    auto* negotiation_body = protocol::ecsp_body(system_negotiation.document.get());
    result.config_version = uint_body_field(negotiation_body, "configVersion");
    result.sequence_id = uint_body_field(negotiation_body, "sequenceId");

    ++seq;
    if (!send_or_fail(
            transport,
            protocol::build_init_sync_result_json(settings, seq, controller_id, options.timestamp_ms),
            seq,
            &result
        )) {
        return result;
    }

    auto init_ack = receive_until(transport, protocol::MessageType::InitSyncResultAck);
    if (!init_ack.ok) {
        return failure(init_ack.error, init_ack.detail, controller_id, username);
    }
    if (protocol::ecsp_header_error(init_ack.document.get()) != 0) {
        return failure(AdoptionError::ControllerRejectedInitSync, "controller rejected INIT_SYNC_RESULT", controller_id, username);
    }

    result.ok = true;
    result.initial_sync_complete = true;
    result.error = AdoptionError::None;
    result.detail.clear();
    return result;
}

} // namespace openomada::lifecycle
