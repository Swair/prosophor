// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#include "common/subprocess_wrapper.h"

#include <chrono>
#include <filesystem>

#include "common/log_wrapper.h"
#include "platform/platform.h"

namespace prosophor {

SubprocessResult ExecuteScriptWithTimeout(const std::string& script_path, int timeout_ms) {
    SubprocessResult result;

    if (!std::filesystem::exists(script_path)) {
        result.return_code = -1;
        result.error_output = "Script not found: " + script_path;
        return result;
    }

    // Make executable if not already
    std::filesystem::permissions(script_path,
        std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);

    int timeout_sec = timeout_ms / 1000;
    if (timeout_sec < 1) timeout_sec = 1;

    auto cmd_result = platform::RunCommandWithOutput(script_path, timeout_sec);

    result.return_code = cmd_result.exit_code;
    result.output = cmd_result.output;

    if (cmd_result.exit_code == -2) {
        result.timeout = true;
        result.error_output = "Script timeout after " + std::to_string(timeout_ms) + "ms";
        LOG_WARN("Script timeout: {}", script_path);
    }

    return result;
}

}  // namespace prosophor
