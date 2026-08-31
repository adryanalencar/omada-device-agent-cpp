#pragma once

#include <cstdint>
#include <string>

#include "openomada/domain/mac_address.hpp"

namespace openomada::application {

struct AgentSettings {
    std::string controller_host{};
    std::uint16_t discovery_port{29810};
    std::uint16_t manage_port{29814};
    std::uint16_t local_discovery_port{0};
    std::uint32_t tcp_timeout_seconds{15};
    std::uint32_t inform_interval_ms{3000};
    std::uint32_t reconnect_delay_ms{3000};
    std::uint32_t managed_reconnect_attempts{3};
    std::string state_file{};
    bool tls_verify{false};
    std::string tls_ca_file{};

    domain::MacAddress mac;
    std::string device_name{"OpenOmada-AP"};
    std::string model{"EAP110"};
    std::string model_version{"4.0"};
    std::string hardware_version{"4.0"};
    std::string firmware_version{"5.0.4"};
    std::uint32_t customize_region{841};
    std::string device_ip{"0.0.0.0"};

    std::string ecsp_version{"2.3.0"};
    std::uint32_t ecsp_ver_cap{2};
    std::string controller_id{};
    std::string site_id{};
    std::string destination_controller_id{};

    std::string device_username{};
    std::string device_password{};
    std::uint32_t device_cipher_type{5};
};

} // namespace openomada::application
