#include "openomada/protocol/frame_codec.hpp"

namespace openomada::protocol {

std::vector<std::uint8_t> encode_frame(std::string_view payload) {
    const auto length = static_cast<std::uint32_t>(payload.size());
    std::vector<std::uint8_t> frame;
    frame.reserve(kLengthSize + payload.size());
    frame.push_back(static_cast<std::uint8_t>((length >> 24) & 0xFFU));
    frame.push_back(static_cast<std::uint8_t>((length >> 16) & 0xFFU));
    frame.push_back(static_cast<std::uint8_t>((length >> 8) & 0xFFU));
    frame.push_back(static_cast<std::uint8_t>(length & 0xFFU));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

DecodeResult decode_frame(const std::uint8_t* data, std::size_t size, std::size_t max_payload) {
    DecodeResult result;
    if (data == nullptr || size < kLengthSize) {
        result.error = FrameError::TooShort;
        return result;
    }

    const std::uint32_t declared =
        (static_cast<std::uint32_t>(data[0]) << 24) |
        (static_cast<std::uint32_t>(data[1]) << 16) |
        (static_cast<std::uint32_t>(data[2]) << 8) |
        static_cast<std::uint32_t>(data[3]);

    result.declared_length = declared;
    result.actual_length = size - kLengthSize;

    if (declared == 0) {
        result.error = FrameError::EmptyPayload;
        return result;
    }
    if (declared > max_payload) {
        result.error = FrameError::PayloadTooLarge;
        return result;
    }
    if (declared != result.actual_length) {
        result.error = FrameError::LengthMismatch;
        return result;
    }

    result.payload.assign(
        reinterpret_cast<const char*>(data + kLengthSize),
        reinterpret_cast<const char*>(data + kLengthSize + declared)
    );
    return result;
}

DecodeResult decode_frame(const std::vector<std::uint8_t>& frame, std::size_t max_payload) {
    return decode_frame(frame.data(), frame.size(), max_payload);
}

} // namespace openomada::protocol

