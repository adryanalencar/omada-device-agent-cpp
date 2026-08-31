#include "openomada/domain/mac_address.hpp"

namespace openomada::domain {
namespace {

int hex_value(char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

char lower_hex(std::uint8_t nibble) noexcept {
    static constexpr char kHex[] = "0123456789abcdef";
    return kHex[nibble & 0x0F];
}

char upper_hex(std::uint8_t nibble) noexcept {
    static constexpr char kHex[] = "0123456789ABCDEF";
    return kHex[nibble & 0x0F];
}

} // namespace

std::optional<MacAddress> MacAddress::parse(std::string_view text) noexcept {
    std::array<int, 12> nibbles{};
    std::size_t count = 0;

    for (char ch : text) {
        const int value = hex_value(ch);
        if (value < 0) {
            continue;
        }
        if (count >= nibbles.size()) {
            return std::nullopt;
        }
        nibbles[count++] = value;
    }

    if (count != nibbles.size()) {
        return std::nullopt;
    }

    std::array<std::uint8_t, 6> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>((nibbles[i * 2] << 4) | nibbles[i * 2 + 1]);
    }
    return MacAddress(bytes);
}

std::string MacAddress::normalized() const {
    std::string out;
    out.reserve(17);
    for (std::size_t i = 0; i < bytes_.size(); ++i) {
        if (i != 0) {
            out.push_back(':');
        }
        out.push_back(lower_hex(static_cast<std::uint8_t>(bytes_[i] >> 4)));
        out.push_back(lower_hex(bytes_[i]));
    }
    return out;
}

std::string MacAddress::omada() const {
    std::string out;
    out.reserve(17);
    for (std::size_t i = 0; i < bytes_.size(); ++i) {
        if (i != 0) {
            out.push_back('-');
        }
        out.push_back(upper_hex(static_cast<std::uint8_t>(bytes_[i] >> 4)));
        out.push_back(upper_hex(bytes_[i]));
    }
    return out;
}

} // namespace openomada::domain

