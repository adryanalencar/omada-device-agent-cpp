#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace openomada::transport {

class SocketFd {
public:
    SocketFd() noexcept = default;
    explicit SocketFd(int fd) noexcept : fd_(fd) {}
    ~SocketFd();

    SocketFd(const SocketFd&) = delete;
    SocketFd& operator=(const SocketFd&) = delete;

    SocketFd(SocketFd&& other) noexcept;
    SocketFd& operator=(SocketFd&& other) noexcept;

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }
    int release() noexcept;
    void reset(int fd = -1) noexcept;

private:
    int fd_{-1};
};

bool set_socket_timeout(int fd, std::uint32_t timeout_seconds, std::string* error);
bool connect_tcp(
    SocketFd& out,
    const std::string& host,
    std::uint16_t port,
    std::uint32_t timeout_seconds,
    std::string* error
);
bool read_exact(int fd, std::uint8_t* buffer, std::size_t size, bool* timeout, std::string* error);
bool write_all(int fd, const std::uint8_t* buffer, std::size_t size, std::string* error);

} // namespace openomada::transport

