#include "openomada/crypto/random.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <openssl/rand.h>

namespace openomada::crypto {
namespace {

char lower_hex(std::uint8_t value) noexcept {
    static constexpr char kHex[] = "0123456789abcdef";
    return kHex[value & 0x0F];
}

} // namespace

std::string random_uuid_v4() {
    std::array<std::uint8_t, 16> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        return {};
    }

    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0FU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3FU) | 0x80U);

    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out.push_back('-');
        }
        out.push_back(lower_hex(static_cast<std::uint8_t>(bytes[i] >> 4)));
        out.push_back(lower_hex(bytes[i]));
    }
    return out;
}

} // namespace openomada::crypto
