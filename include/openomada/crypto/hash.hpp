#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace openomada::crypto {

std::string upper_md5(std::string_view text);
std::string upper_sha256(const std::uint8_t* data, std::size_t size);
std::string upper_sha256(std::string_view text);

} // namespace openomada::crypto

