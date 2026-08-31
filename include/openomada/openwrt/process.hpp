#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "openomada/openwrt/opennds.hpp"
#include "openomada/openwrt/uci.hpp"

namespace openomada::openwrt {

struct ProcessResult {
    bool ok{false};
    int exit_code{-1};
    std::string output{};
    std::string error{};
};

ProcessResult run_process(
    const std::vector<std::string>& command,
    std::string_view input = {},
    std::size_t max_output_bytes = 65536
) noexcept;

bool command_available(std::string_view name) noexcept;

class OpenWrtProcessExecutor final : public UciExecutor, public OpenNdsExecutor {
public:
    UciExecutionResult apply_batch(const std::string& batch) override;
    UciExecutionResult reload_wifi() override;

    OpenNdsCommandResult run(
        const std::vector<std::string>& command,
        std::string_view input = {}
    ) override;
};

} // namespace openomada::openwrt
