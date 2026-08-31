#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "openomada/openwrt/process.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    require(input.good(), "file opens");
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void test_run_process_uses_argv_and_captures_output() {
    const auto result = openomada::openwrt::run_process({"cat"}, "hello\n");

    require(result.ok, result.error.c_str());
    require(result.exit_code == 0, "exit code");
    require(result.output == "hello\n", "stdout captured");
}

void test_run_process_reports_non_zero_exit() {
    const auto result = openomada::openwrt::run_process({"false"});

    require(!result.ok, "false should fail");
    require(result.exit_code != 0, "non-zero exit code");
}

void test_command_available_searches_path_without_slashes() {
    require(openomada::openwrt::command_available("sh"), "sh available");
    require(!openomada::openwrt::command_available("/bin/sh"), "slash rejected");
}

void test_openwrt_process_executor_writes_files_atomically() {
    const std::string path = "/tmp/openomada-process-test-" + std::to_string(static_cast<long long>(::getpid()));
    openomada::openwrt::OpenWrtProcessExecutor executor;

    const auto result = executor.run({"write-file", path}, "#!/bin/sh\n");

    require(result.ok, result.error.c_str());
    require(read_file(path) == "#!/bin/sh\n", "content written");
    (void)::unlink(path.c_str());
}

void test_uci_empty_batch_is_noop() {
    openomada::openwrt::OpenWrtProcessExecutor executor;
    const auto result = executor.apply_batch("\n\t\n");

    require(result.ok, result.error.c_str());
    require(!result.changed, "empty batch unchanged");
    require(result.command_count == 0, "empty command count");
}

} // namespace

int main() {
    test_run_process_uses_argv_and_captures_output();
    test_run_process_reports_non_zero_exit();
    test_command_available_searches_path_without_slashes();
    test_openwrt_process_executor_writes_files_atomically();
    test_uci_empty_batch_is_noop();
    std::cout << "openomada-openwrt-process-tests passed\n";
    return 0;
}
