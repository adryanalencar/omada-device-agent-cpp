#include "openomada/persistence/session_state_repository.hpp"

#include "openomada/protocol/ecsp_message.hpp"
#include "openomada/protocol/json.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace openomada::persistence {
namespace {

constexpr std::uint32_t kStateVersion = 1;
constexpr off_t kMaxStateFileBytes = 64 * 1024;

class UniqueFd {
public:
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}
    ~UniqueFd() { close(); }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }

    void close() noexcept {
        if (fd_ >= 0) {
            (void)::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_{-1};
};

std::string errno_message(std::string_view operation, std::string_view path) {
    std::string out(operation);
    out += " ";
    out += path;
    out += ": ";
    out += std::strerror(errno);
    return out;
}

std::string parent_directory(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos) {
        return {};
    }
    if (slash == 0) {
        return "/";
    }
    return std::string(path.substr(0, slash));
}

RepositoryStatus mkdir_p(const std::string& directory) {
    if (directory.empty() || directory == ".") {
        return RepositoryStatus::success();
    }

    std::string current;
    current.reserve(directory.size());
    std::size_t index = 0;
    if (!directory.empty() && directory[0] == '/') {
        current = "/";
        index = 1;
    }

    while (index <= directory.size()) {
        const std::size_t next = directory.find('/', index);
        const std::string_view part = next == std::string::npos
            ? std::string_view(directory).substr(index)
            : std::string_view(directory).substr(index, next - index);
        if (!part.empty()) {
            if (!current.empty() && current.back() != '/') {
                current.push_back('/');
            }
            current.append(part);
            if (::mkdir(current.c_str(), 0700) != 0 && errno != EEXIST) {
                return RepositoryStatus::failure(errno_message("mkdir", current));
            }
        }
        if (next == std::string::npos) {
            break;
        }
        index = next + 1;
    }
    return RepositoryStatus::success();
}

bool write_all(int fd, std::string_view data) {
    const char* ptr = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        const ssize_t written = ::write(fd, ptr, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (written == 0) {
            return false;
        }
        ptr += written;
        remaining -= static_cast<std::size_t>(written);
    }
    return true;
}

std::optional<std::string> read_file_limited(const std::string& path, std::string* error) {
    UniqueFd fd(::open(path.c_str(), O_RDONLY));
    if (!fd.valid()) {
        if (errno == ENOENT) {
            return std::nullopt;
        }
        if (error != nullptr) {
            *error = errno_message("open", path);
        }
        return std::nullopt;
    }

    struct stat st {};
    if (::fstat(fd.get(), &st) != 0) {
        if (error != nullptr) {
            *error = errno_message("fstat", path);
        }
        return std::nullopt;
    }
    if (st.st_size < 0 || st.st_size > kMaxStateFileBytes) {
        if (error != nullptr) {
            *error = "managed-state file is too large";
        }
        return std::nullopt;
    }

    std::string data;
    data.reserve(static_cast<std::size_t>(st.st_size));
    char buffer[1024];
    while (true) {
        const ssize_t read_count = ::read(fd.get(), buffer, sizeof(buffer));
        if (read_count < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (error != nullptr) {
                *error = errno_message("read", path);
            }
            return std::nullopt;
        }
        if (read_count == 0) {
            break;
        }
        data.append(buffer, static_cast<std::size_t>(read_count));
        if (data.size() > static_cast<std::size_t>(kMaxStateFileBytes)) {
            if (error != nullptr) {
                *error = "managed-state file exceeded size limit";
            }
            return std::nullopt;
        }
    }
    return data;
}

std::optional<std::string> required_string(json_object* root, const char* key) {
    return protocol::json_string(protocol::object_member(root, key));
}

std::optional<std::uint32_t> optional_uint32(json_object* root, const char* key, std::string* error) {
    json_object* raw = protocol::object_member(root, key);
    if (raw == nullptr || json_object_is_type(raw, json_type_null)) {
        return std::nullopt;
    }
    auto value = protocol::json_int(raw);
    if (!value.has_value() || *value < 0 || *value > UINT32_MAX) {
        if (error != nullptr) {
            *error = std::string("invalid managed-state integer field: ") + key;
        }
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

std::string quoted(std::string_view value) {
    std::string out = "\"";
    out += protocol::json_escape(value);
    out += "\"";
    return out;
}

void append_optional_uint(std::string* out, const char* key, std::optional<std::uint32_t> value) {
    *out += "  \"";
    *out += key;
    *out += "\": ";
    if (value.has_value()) {
        *out += std::to_string(*value);
    } else {
        *out += "null";
    }
    *out += ",\n";
}

std::string state_json(const lifecycle::ManagedState& state, const domain::MacAddress& parsed_mac) {
    std::string out;
    out.reserve(384);
    out += "{\n";
    append_optional_uint(&out, "config_version", state.config_version);
    out += "  \"controller_host\": ";
    out += quoted(state.controller_host);
    out += ",\n";
    out += "  \"controller_id\": ";
    out += quoted(state.controller_id);
    out += ",\n";
    out += "  \"mac\": ";
    out += quoted(parsed_mac.normalized());
    out += ",\n";
    out += "  \"manage_port\": ";
    out += std::to_string(state.manage_port);
    out += ",\n";
    append_optional_uint(&out, "sequence_id", state.sequence_id);
    out += "  \"site_id\": ";
    out += quoted(state.site_id);
    out += ",\n";
    out += "  \"updated_at\": ";
    out += std::to_string(state.updated_at);
    out += ",\n";
    out += "  \"username\": ";
    out += quoted(state.username);
    out += ",\n";
    out += "  \"version\": ";
    out += std::to_string(kStateVersion);
    out += "\n}\n";
    return out;
}

LoadStateResult ignored_state(std::string message) {
    LoadStateResult result;
    result.ok = true;
    result.found = false;
    result.error = std::move(message);
    return result;
}

} // namespace

RepositoryStatus RepositoryStatus::success() {
    return {true, {}};
}

RepositoryStatus RepositoryStatus::failure(std::string message) {
    return {false, std::move(message)};
}

JsonSessionStateRepository::JsonSessionStateRepository(
    std::string path,
    domain::MacAddress device_mac,
    std::string controller_host
) : path_(std::move(path)),
    device_mac_(device_mac),
    controller_host_(std::move(controller_host)) {}

LoadStateResult JsonSessionStateRepository::load() const {
    std::string read_error;
    auto data = read_file_limited(path_, &read_error);
    if (!data.has_value()) {
        if (read_error.empty()) {
            return {true, false, {}, {}};
        }
        return {false, false, {}, read_error};
    }

    auto document = protocol::JsonDocument::parse(*data);
    if (!document.valid()) {
        return ignored_state("managed-state root is not a JSON object");
    }
    json_object* root = document.get();

    auto version = protocol::json_int(protocol::object_member(root, "version"));
    if (!version.has_value() || *version != kStateVersion) {
        return ignored_state("unsupported managed-state version");
    }

    auto raw_mac = required_string(root, "mac");
    auto controller_host = required_string(root, "controller_host");
    auto controller_id = required_string(root, "controller_id");
    auto manage_port = protocol::json_int(protocol::object_member(root, "manage_port"));
    if (!raw_mac.has_value() || !controller_host.has_value() || !controller_id.has_value() || !manage_port.has_value()) {
        return ignored_state("managed-state is missing required identity fields");
    }

    auto parsed_mac = domain::MacAddress::parse(*raw_mac);
    if (!parsed_mac.has_value()) {
        return ignored_state("managed-state MAC is invalid");
    }
    if (parsed_mac->normalized() != device_mac_.normalized()) {
        return ignored_state("managed-state belongs to another device MAC");
    }
    if (*controller_host != controller_host_) {
        return ignored_state("managed-state belongs to another controller host");
    }
    if (controller_id->empty() || *manage_port <= 0 || *manage_port > 65535) {
        return ignored_state("managed-state is incomplete");
    }

    lifecycle::ManagedState state;
    state.version = kStateVersion;
    state.mac = parsed_mac->normalized();
    state.controller_host = *controller_host;
    state.controller_id = *controller_id;
    state.manage_port = static_cast<std::uint16_t>(*manage_port);
    state.site_id = required_string(root, "site_id").value_or("");
    state.username = required_string(root, "username").value_or("");

    std::string field_error;
    state.config_version = optional_uint32(root, "config_version", &field_error);
    if (!field_error.empty()) {
        return ignored_state(field_error);
    }
    state.sequence_id = optional_uint32(root, "sequence_id", &field_error);
    if (!field_error.empty()) {
        return ignored_state(field_error);
    }
    auto updated_at = protocol::json_int(protocol::object_member(root, "updated_at"));
    if (updated_at.has_value() && *updated_at >= 0) {
        state.updated_at = static_cast<std::uint64_t>(*updated_at);
    }

    return {true, true, std::move(state), {}};
}

RepositoryStatus JsonSessionStateRepository::save(const lifecycle::ManagedState& state) const {
    if (state.version != kStateVersion) {
        return RepositoryStatus::failure("managed-state version is unsupported");
    }
    auto parsed_mac = domain::MacAddress::parse(state.mac);
    if (!parsed_mac.has_value()) {
        return RepositoryStatus::failure("managed-state MAC is invalid");
    }
    if (parsed_mac->normalized() != device_mac_.normalized() || state.controller_host != controller_host_) {
        return RepositoryStatus::failure("managed-state identity does not match repository identity");
    }
    if (state.controller_id.empty() || state.manage_port == 0) {
        return RepositoryStatus::failure("managed-state is incomplete");
    }

    auto mkdir_status = mkdir_p(parent_directory(path_));
    if (!mkdir_status.ok) {
        return mkdir_status;
    }

    const std::string tmp_path = path_ + ".tmp";
    UniqueFd fd(::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600));
    if (!fd.valid()) {
        return RepositoryStatus::failure(errno_message("open", tmp_path));
    }

    const std::string data = state_json(state, *parsed_mac);
    if (!write_all(fd.get(), data)) {
        return RepositoryStatus::failure(errno_message("write", tmp_path));
    }
    if (::fsync(fd.get()) != 0) {
        return RepositoryStatus::failure(errno_message("fsync", tmp_path));
    }
    fd.close();

    if (::rename(tmp_path.c_str(), path_.c_str()) != 0) {
        return RepositoryStatus::failure(errno_message("rename", path_));
    }

    const std::string parent = parent_directory(path_);
    if (!parent.empty()) {
        UniqueFd parent_fd(::open(parent.c_str(), O_RDONLY));
        if (parent_fd.valid()) {
            (void)::fsync(parent_fd.get());
        }
    }
    return RepositoryStatus::success();
}

bool JsonSessionStateRepository::clear() const {
    if (::unlink(path_.c_str()) == 0) {
        return true;
    }
    return errno == ENOENT;
}

} // namespace openomada::persistence
