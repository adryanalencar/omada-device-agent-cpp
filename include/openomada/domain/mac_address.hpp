#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace openomada::domain {

class MacAddress {
public:
    MacAddress() noexcept = default;

    static std::optional<MacAddress> parse(std::string_view text) noexcept;

    const std::array<std::uint8_t, 6>& bytes() const noexcept { return bytes_; }

    std::string normalized() const;
    std::string omada() const;

    friend bool operator==(const MacAddress& lhs, const MacAddress& rhs) noexcept {
        return lhs.bytes_ == rhs.bytes_;
    }

    friend bool operator!=(const MacAddress& lhs, const MacAddress& rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    explicit MacAddress(std::array<std::uint8_t, 6> bytes) noexcept : bytes_(bytes) {}

    std::array<std::uint8_t, 6> bytes_{};
};

} // namespace openomada::domain
