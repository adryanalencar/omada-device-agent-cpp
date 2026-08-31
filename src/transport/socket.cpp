#include "openomada/transport/socket.hpp"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace openomada::transport {
namespace {

void assign_errno(std::string* out, const char* prefix) {
    if (out == nullptr) {
        return;
    }
    *out = prefix;
    *out += ": ";
    *out += std::strerror(errno);
}

timeval timeout_value(std::uint32_t seconds) noexcept {
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(seconds);
    tv.tv_usec = 0;
    return tv;
}

bool set_nonblocking(int fd, bool enabled, std::string* error) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        assign_errno(error, "fcntl(F_GETFL)");
        return false;
    }
    const int next = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(fd, F_SETFL, next) < 0) {
        assign_errno(error, "fcntl(F_SETFL)");
        return false;
    }
    return true;
}

} // namespace

SocketFd::~SocketFd() {
    reset();
}

SocketFd::SocketFd(SocketFd&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

SocketFd& SocketFd::operator=(SocketFd&& other) noexcept {
    if (this != &other) {
        reset();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

int SocketFd::release() noexcept {
    const int fd = fd_;
    fd_ = -1;
    return fd;
}

void SocketFd::reset(int fd) noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
    }
    fd_ = fd;
}

bool set_socket_timeout(int fd, std::uint32_t timeout_seconds, std::string* error) {
    const timeval tv = timeout_value(timeout_seconds);
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        assign_errno(error, "setsockopt(SO_RCVTIMEO)");
        return false;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        assign_errno(error, "setsockopt(SO_SNDTIMEO)");
        return false;
    }
    return true;
}

bool connect_tcp(
    SocketFd& out,
    const std::string& host,
    std::uint16_t port,
    std::uint32_t timeout_seconds,
    std::string* error
) {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    addrinfo* addresses = nullptr;
    const std::string port_text = std::to_string(port);
    const int gai = getaddrinfo(host.c_str(), port_text.c_str(), &hints, &addresses);
    if (gai != 0) {
        if (error != nullptr) {
            *error = "getaddrinfo: ";
            *error += gai_strerror(gai);
        }
        return false;
    }

    bool connected = false;
    std::string last_error;
    for (addrinfo* ai = addresses; ai != nullptr; ai = ai->ai_next) {
        SocketFd candidate(socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (!candidate.valid()) {
            assign_errno(&last_error, "socket");
            continue;
        }
        if (!set_nonblocking(candidate.get(), true, &last_error)) {
            continue;
        }

        const int rc = ::connect(candidate.get(), ai->ai_addr, ai->ai_addrlen);
        if (rc == 0) {
            connected = true;
        } else if (errno == EINPROGRESS) {
            fd_set write_set;
            FD_ZERO(&write_set);
            FD_SET(candidate.get(), &write_set);
            timeval tv = timeout_value(timeout_seconds);
            const int selected = select(candidate.get() + 1, nullptr, &write_set, nullptr, &tv);
            if (selected > 0) {
                int so_error = 0;
                socklen_t so_error_len = sizeof(so_error);
                if (getsockopt(candidate.get(), SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) == 0 && so_error == 0) {
                    connected = true;
                } else {
                    errno = so_error;
                    assign_errno(&last_error, "connect");
                }
            } else if (selected == 0) {
                last_error = "connect: timeout";
            } else {
                assign_errno(&last_error, "select");
            }
        } else {
            assign_errno(&last_error, "connect");
        }

        if (connected) {
            if (!set_nonblocking(candidate.get(), false, &last_error)) {
                connected = false;
                continue;
            }
            if (!set_socket_timeout(candidate.get(), timeout_seconds, &last_error)) {
                connected = false;
                continue;
            }
            out = std::move(candidate);
            break;
        }
    }

    freeaddrinfo(addresses);
    if (!connected && error != nullptr) {
        *error = last_error.empty() ? "connect: no usable address" : last_error;
    }
    return connected;
}

bool read_exact(int fd, std::uint8_t* buffer, std::size_t size, bool* timeout, std::string* error) {
    if (timeout != nullptr) {
        *timeout = false;
    }
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t rc = recv(fd, buffer + offset, size - offset, 0);
        if (rc > 0) {
            offset += static_cast<std::size_t>(rc);
            continue;
        }
        if (rc == 0) {
            if (error != nullptr) {
                *error = "peer closed connection";
            }
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (timeout != nullptr) {
                *timeout = true;
            }
            if (error != nullptr) {
                *error = "read timeout";
            }
            return false;
        }
        assign_errno(error, "recv");
        return false;
    }
    return true;
}

bool write_all(int fd, const std::uint8_t* buffer, std::size_t size, std::string* error) {
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t rc = send(fd, buffer + offset, size - offset, 0);
        if (rc > 0) {
            offset += static_cast<std::size_t>(rc);
            continue;
        }
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        assign_errno(error, "send");
        return false;
    }
    return true;
}

} // namespace openomada::transport

