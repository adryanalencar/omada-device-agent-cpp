#pragma once

#include <string>
#include <string_view>

namespace openomada::crypto {

std::string calculate_ecsp2_auth(
    std::string_view username,
    std::string_view encrypted_password,
    std::string_view random_key
);

std::string calculate_md5_mode_auth(
    std::string_view username,
    std::string_view plain_password,
    std::string_view random_key
);

} // namespace openomada::crypto

