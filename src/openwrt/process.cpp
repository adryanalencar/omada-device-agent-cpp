#include "openomada/openwrt/process.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace openomada::openwrt {
namespace {

class UniqueFd {
public:
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}
    ~UniqueFd() { reset(); }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }

    int release() noexcept {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_{-1};
};

ProcessResult process_error(std::string error) {
    ProcessResult result;
    result.error = std::move(error);
    return result;
}

std::string errno_text(const char* prefix) {
    std::string out(prefix);
    out += ": ";
    out += std::strerror(errno);
    return out;
}

bool valid_argument(std::string_view value) noexcept {
    return value.find('\0') == std::string_view::npos;
}

bool write_all_fd(int fd, std::string_view data) noexcept {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t written = ::write(fd, data.data() + offset, data.size() - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

std::string parent_directory(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return std::string(path.substr(0, slash));
}

ProcessResult write_file_atomically(const std::string& path, std::string_view content) noexcept {
    if (path.empty() || path.find('\0') != std::string::npos) {
        return process_error("write-file path is invalid");
    }
    const std::string tmp_path = path + ".tmp";
    UniqueFd fd(::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644));
    if (!fd.valid()) {
        return process_error(errno_text("open"));
    }
    if (!write_all_fd(fd.get(), content)) {
        return process_error(errno_text("write"));
    }
    if (::fsync(fd.get()) != 0) {
        return process_error(errno_text("fsync"));
    }
    fd.reset();
    if (::rename(tmp_path.c_str(), path.c_str()) != 0) {
        return process_error(errno_text("rename"));
    }

    const std::string parent = parent_directory(path);
    UniqueFd parent_fd(::open(parent.c_str(), O_RDONLY));
    if (parent_fd.valid()) {
        (void)::fsync(parent_fd.get());
    }
    ProcessResult result;
    result.ok = true;
    result.exit_code = 0;
    return result;
}

std::size_t command_count(std::string_view batch) noexcept {
    std::size_t count = 0;
    std::size_t offset = 0;
    while (offset <= batch.size()) {
        const std::size_t newline = batch.find('\n', offset);
        const std::size_t end = newline == std::string_view::npos ? batch.size() : newline;
        const std::string_view line = batch.substr(offset, end - offset);
        bool non_ws = false;
        for (char ch : line) {
            if (ch != ' ' && ch != '\t' && ch != '\r') {
                non_ws = true;
                break;
            }
        }
        if (non_ws) {
            ++count;
        }
        if (newline == std::string_view::npos) {
            break;
        }
        offset = newline + 1;
    }
    return count;
}

OpenNdsCommandResult convert_process_result(ProcessResult result) {
    OpenNdsCommandResult converted;
    converted.ok = result.ok;
    converted.exit_code = result.exit_code;
    converted.output = std::move(result.output);
    converted.error = std::move(result.error);
    return converted;
}

UciExecutionResult convert_uci_result(ProcessResult result, std::size_t commands) {
    UciExecutionResult converted;
    converted.ok = result.ok;
    converted.changed = result.ok && commands > 0;
    converted.command_count = commands;
    converted.error = result.ok ? std::string() : (result.error.empty() ? result.output : result.error);
    return converted;
}

} // namespace

ProcessResult run_process(
    const std::vector<std::string>& command,
    std::string_view input,
    std::size_t max_output_bytes
) noexcept {
    if (command.empty() || command[0].empty()) {
        return process_error("command is empty");
    }
    for (const auto& argument : command) {
        if (!valid_argument(argument)) {
            return process_error("command argument contains NUL byte");
        }
    }

    int stdin_pipe_raw[2] = {-1, -1};
    int output_pipe_raw[2] = {-1, -1};
    if (::pipe(stdin_pipe_raw) != 0) {
        return process_error(errno_text("pipe"));
    }
    UniqueFd stdin_read(stdin_pipe_raw[0]);
    UniqueFd stdin_write(stdin_pipe_raw[1]);
    if (::pipe(output_pipe_raw) != 0) {
        return process_error(errno_text("pipe"));
    }
    UniqueFd output_read(output_pipe_raw[0]);
    UniqueFd output_write(output_pipe_raw[1]);

    const pid_t pid = ::fork();
    if (pid < 0) {
        return process_error(errno_text("fork"));
    }
    if (pid == 0) {
        if (::dup2(stdin_read.get(), STDIN_FILENO) < 0 ||
            ::dup2(output_write.get(), STDOUT_FILENO) < 0 ||
            ::dup2(output_write.get(), STDERR_FILENO) < 0) {
            _exit(126);
        }
        stdin_read.reset();
        stdin_write.reset();
        output_read.reset();
        output_write.reset();

        std::vector<char*> argv;
        argv.reserve(command.size() + 1);
        for (const auto& argument : command) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        ::execvp(argv[0], argv.data());
        _exit(127);
    }

    stdin_read.reset();
    output_write.reset();

    if (!input.empty() && !write_all_fd(stdin_write.get(), input)) {
        stdin_write.reset();
        int status = 0;
        (void)::waitpid(pid, &status, 0);
        return process_error(errno_text("write stdin"));
    }
    stdin_write.reset();

    ProcessResult result;
    result.output.reserve(256);
    char buffer[4096];
    while (true) {
        const ssize_t read_bytes = ::read(output_read.get(), buffer, sizeof(buffer));
        if (read_bytes > 0) {
            const std::size_t available = max_output_bytes > result.output.size()
                ? max_output_bytes - result.output.size()
                : 0;
            const std::size_t copied = std::min<std::size_t>(
                available,
                static_cast<std::size_t>(read_bytes)
            );
            result.output.append(buffer, copied);
            continue;
        }
        if (read_bytes < 0 && errno == EINTR) {
            continue;
        }
        if (read_bytes < 0) {
            result.error = errno_text("read output");
        }
        break;
    }
    output_read.reset();

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        result.error = errno_text("waitpid");
        return result;
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
        result.ok = result.exit_code == 0 && result.error.empty();
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
        result.ok = false;
    } else {
        result.exit_code = -1;
        result.ok = false;
    }
    if (!result.ok && result.error.empty()) {
        result.error = result.output.empty()
            ? ("process exited with code " + std::to_string(result.exit_code))
            : result.output;
    }
    return result;
}

bool command_available(std::string_view name) noexcept {
    if (name.empty() || name.find('/') != std::string_view::npos || name.find('\0') != std::string_view::npos) {
        return false;
    }
    const char* raw_path = std::getenv("PATH");
    const std::string path = raw_path == nullptr || *raw_path == '\0'
        ? "/sbin:/usr/sbin:/bin:/usr/bin"
        : raw_path;
    std::size_t offset = 0;
    while (offset <= path.size()) {
        const std::size_t colon = path.find(':', offset);
        const std::size_t end = colon == std::string::npos ? path.size() : colon;
        std::string candidate = path.substr(offset, end - offset);
        if (candidate.empty()) {
            candidate = ".";
        }
        candidate += "/";
        candidate += std::string(name);
        if (::access(candidate.c_str(), X_OK) == 0) {
            return true;
        }
        if (colon == std::string::npos) {
            break;
        }
        offset = colon + 1;
    }
    return false;
}

UciExecutionResult OpenWrtProcessExecutor::apply_batch(const std::string& batch) {
    const std::size_t commands = command_count(batch);
    if (commands == 0) {
        return {true, false, {}, 0};
    }
    return convert_uci_result(run_process({"uci", "batch"}, batch), commands);
}

UciExecutionResult OpenWrtProcessExecutor::reload_wifi() {
    return convert_uci_result(run_process({"wifi", "reload"}), 1);
}

OpenNdsCommandResult OpenWrtProcessExecutor::run(
    const std::vector<std::string>& command,
    std::string_view input
) {
    if (!command.empty() && command[0] == "write-file") {
        if (command.size() != 2) {
            return {false, 2, {}, "write-file requires a path"};
        }
        return convert_process_result(write_file_atomically(command[1], input));
    }
    return convert_process_result(run_process(command, input));
}

} // namespace openomada::openwrt
