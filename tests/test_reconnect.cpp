#include <cstdlib>
#include <iostream>
#include <string>

#include "openomada/application/settings.hpp"
#include "openomada/domain/mac_address.hpp"
#include "openomada/lifecycle/reconnect.hpp"
#include "openomada/lifecycle/session.hpp"
#include "openomada/persistence/session_state_repository.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

openomada::domain::MacAddress must_parse_mac(const char* value) {
    auto parsed = openomada::domain::MacAddress::parse(value);
    require(parsed.has_value(), "MAC should parse");
    return *parsed;
}

openomada::lifecycle::ManagedState managed_state() {
    openomada::lifecycle::ManagedState state;
    state.version = 1;
    state.mac = "02:11:22:33:44:55";
    state.controller_host = "controller.example.test";
    state.controller_id = "0123456789abcdef0123456789abcdef";
    state.manage_port = 29814;
    state.site_id = "0123456789abcdef01234567";
    state.username = "lab-user";
    state.config_version = 7;
    state.sequence_id = 9;
    state.updated_at = 1780000000;
    return state;
}

class FakeRepository final : public openomada::persistence::SessionStateRepository {
public:
    openomada::persistence::LoadStateResult load_result{true, true, managed_state(), {}};

    openomada::persistence::LoadStateResult load() const override {
        return load_result;
    }

    openomada::persistence::RepositoryStatus save(const openomada::lifecycle::ManagedState&) const override {
        return openomada::persistence::RepositoryStatus::success();
    }

    bool clear() const override {
        return true;
    }
};

void test_settings_and_options_for_managed_reconnect_preserve_nonsecret_state() {
    openomada::application::AgentSettings settings;
    settings.controller_host = "other-controller.example.test";
    settings.mac = must_parse_mac("02:11:22:33:44:55");
    settings.device_password = "runtime-secret";

    const auto state = managed_state();
    const auto managed_settings = openomada::lifecycle::settings_for_managed_state(settings, state);
    const auto options = openomada::lifecycle::adoption_options_for_managed_state(state);

    require(managed_settings.controller_host == state.controller_host, "controller host restored");
    require(managed_settings.controller_id == state.controller_id, "controller id restored");
    require(managed_settings.manage_port == state.manage_port, "manage port restored");
    require(managed_settings.site_id == state.site_id, "site id restored");
    require(managed_settings.device_username == state.username, "username restored when settings empty");
    require(managed_settings.device_password == "runtime-secret", "password stays runtime-only");
    require(options.managed_reconnect, "managed reconnect option");
    require(options.known_config_version.value_or(0) == 7, "known config version restored");
}

void test_direct_reconnect_succeeds_before_rediscovery() {
    FakeRepository repository;
    std::uint32_t attempts = 0;
    bool rediscovery_called = false;

    auto result = openomada::lifecycle::run_managed_reconnect(
        repository,
        {3},
        [&attempts](const openomada::lifecycle::ManagedState& state) {
            ++attempts;
            openomada::lifecycle::AdoptionResult adoption;
            adoption.controller_id = state.controller_id;
            if (attempts == 2) {
                adoption.ok = true;
                adoption.initial_sync_complete = true;
                adoption.username = state.username;
                adoption.config_version = state.config_version;
                adoption.sequence_id = state.sequence_id;
            } else {
                adoption.error = openomada::lifecycle::AdoptionError::ReceiveFailed;
                adoption.detail = "timeout";
            }
            return adoption;
        },
        [&rediscovery_called](const openomada::lifecycle::ManagedState&) {
            rediscovery_called = true;
            return openomada::transport::TransportStatus::success();
        }
    );

    require(result.outcome == openomada::lifecycle::ManagedReconnectOutcome::DirectReconnectSucceeded, "direct reconnect succeeds");
    require(result.attempts == 2, "two direct attempts");
    require(!rediscovery_called, "rediscovery not called after success");
    require(result.adoption.config_version.value_or(0) == 7, "adoption result retained");
}

void test_reconnect_exhaustion_sends_managed_rediscovery() {
    FakeRepository repository;
    std::uint32_t attempts = 0;
    std::uint32_t rediscovery = 0;
    openomada::lifecycle::ManagedState rediscovered_state;

    auto result = openomada::lifecycle::run_managed_reconnect(
        repository,
        {2},
        [&attempts](const openomada::lifecycle::ManagedState&) {
            ++attempts;
            openomada::lifecycle::AdoptionResult adoption;
            adoption.error = openomada::lifecycle::AdoptionError::ReceiveFailed;
            adoption.detail = "controller closed before PRE_CONNECT_INFO_RESPONSE";
            return adoption;
        },
        [&rediscovery, &rediscovered_state](const openomada::lifecycle::ManagedState& state) {
            ++rediscovery;
            rediscovered_state = state;
            return openomada::transport::TransportStatus::success();
        }
    );

    require(result.outcome == openomada::lifecycle::ManagedReconnectOutcome::RediscoverySent, "rediscovery sent after exhausted reconnect");
    require(result.attempts == 2, "direct attempts capped");
    require(attempts == 2, "attempt callback count");
    require(rediscovery == 1, "one rediscovery");
    require(rediscovered_state.mac == "02:11:22:33:44:55", "rediscovery keeps managed MAC");
    require(rediscovered_state.site_id == "0123456789abcdef01234567", "rediscovery keeps site scope");
}

void test_no_state_skips_reconnect() {
    FakeRepository repository;
    repository.load_result = {true, false, {}, {}};
    bool attempted = false;

    auto result = openomada::lifecycle::run_managed_reconnect(
        repository,
        {3},
        [&attempted](const openomada::lifecycle::ManagedState&) {
            attempted = true;
            return openomada::lifecycle::AdoptionResult{};
        },
        [](const openomada::lifecycle::ManagedState&) {
            return openomada::transport::TransportStatus::success();
        }
    );

    require(result.outcome == openomada::lifecycle::ManagedReconnectOutcome::NoState, "no state outcome");
    require(result.attempts == 0, "no direct attempts");
    require(!attempted, "adoption not attempted");
}

void test_state_load_failure_is_reported() {
    FakeRepository repository;
    repository.load_result = {false, false, {}, "read failed"};

    auto result = openomada::lifecycle::run_managed_reconnect(
        repository,
        {3},
        [](const openomada::lifecycle::ManagedState&) {
            return openomada::lifecycle::AdoptionResult{};
        },
        [](const openomada::lifecycle::ManagedState&) {
            return openomada::transport::TransportStatus::success();
        }
    );

    require(result.outcome == openomada::lifecycle::ManagedReconnectOutcome::StateLoadFailed, "state load failure outcome");
    require(result.error == "read failed", "state load error retained");
}

} // namespace

int main() {
    test_settings_and_options_for_managed_reconnect_preserve_nonsecret_state();
    test_direct_reconnect_succeeds_before_rediscovery();
    test_reconnect_exhaustion_sends_managed_rediscovery();
    test_no_state_skips_reconnect();
    test_state_load_failure_is_reported();
    std::cout << "openomada-reconnect-tests passed\n";
    return 0;
}
