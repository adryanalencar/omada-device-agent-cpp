#pragma once

#include <cstdint>

namespace openomada::protocol {

enum class MessageType : std::uint32_t {
    Discovery = 1,
    PreAdoptRequest = 2,
    PreConnectInfo = 3,
    AdoptRequest = 16,
    AdoptResponse = 32,
    EventPortalQuery = 64,
    EventPortalAuth = 128,
    EventPortalAuthResponse = 352,
    NotifyRequest = 80,
    NotifyReply = 144,
    InformRequest = 256,
    InformResponse = 512,
    SetRequest = 4096,
    SetResponse = 8192,
    ForgetRequest = 16384,
    ForgetResponse = 20480,
    InitSync = 4352,
    GetRequest = 24576,
    GetResponse = 28672,
    ForgetRequestNoReset = 131072,
    ForgetResponseNoReset = 196608,
    PreConnectInfoResponse = 0x100000,
    DeviceVerifyInfo = 0x100001,
    DeviceVerifyResponse = 0x100002,
    SystemVerifyResult = 0x100003,
    DeviceNegotiation = 0x100004,
    SystemNegotiation = 0x100005,
    InitSyncResult = 0x100006,
    NotifyRequestV2 = 0x100007,
    NotifyReplyV2 = 0x100008,
    VerifyResultAck = 0x100009,
    InitSyncResultAck = 0x10000A,
    Report = 0x150000,
};

constexpr std::uint32_t to_underlying(MessageType type) noexcept {
    return static_cast<std::uint32_t>(type);
}

} // namespace openomada::protocol

