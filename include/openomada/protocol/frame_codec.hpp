#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openomada::protocol {

constexpr std::size_t kLengthSize = 4;
constexpr std::size_t kMaxDiscoveryPayload = 2000;
constexpr std::size_t kMaxTcpPayload = 8U * 1024U * 1024U;

enum class FrameError {
    None,
    TooShort,
    EmptyPayload,
    PayloadTooLarge,
    LengthMismatch,
};

struct DecodeResult {
    FrameError error{FrameError::None};
    std::uint32_t declared_length{0};
    std::size_t actual_length{0};
    std::string payload{};

    bool ok() const noexcept { return error == FrameError::None; }
};

std::vector<std::uint8_t> encode_frame(std::string_view payload);

DecodeResult decode_frame(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t max_payload = kMaxTcpPayload
);

DecodeResult decode_frame(
    const std::vector<std::uint8_t>& frame,
    std::size_t max_payload = kMaxTcpPayload
);

} // namespace openomada::protocol

