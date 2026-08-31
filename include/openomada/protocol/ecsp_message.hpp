#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "openomada/domain/mac_address.hpp"
#include "openomada/protocol/message_type.hpp"

namespace openomada::protocol {

struct EcspHeader {
    std::optional<std::uint32_t> seq{1};
    std::string version{"2.3.0"};
    std::uint32_t ver_cap{2};
    std::string device{"ap"};
    domain::MacAddress mac;
    MessageType type{MessageType::Discovery};
    std::int32_t error{0};
    std::string dest{};
    std::optional<std::uint64_t> timestamp{};
};

std::string json_escape(std::string_view value);

std::string build_message_json(
    const EcspHeader& header,
    std::string_view body_json = "{}",
    bool include_body = true
);

} // namespace openomada::protocol

