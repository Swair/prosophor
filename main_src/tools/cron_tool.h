// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <functional>
#include <nlohmann/json.hpp>

namespace aicode {

/// Cron tool for LLM function calling
class CronTool {
public:
    /// Execute cron-related operations
    /// @param action One of: create, list, delete, run, pause, resume
    /// @param params Action-specific parameters
    /// @return JSON result or error message
    static std::string Execute(const std::string& action, const nlohmann::json& params);

private:
    /// Create a scheduled task
    static std::string Create(const nlohmann::json& params);

    /// List all scheduled tasks
    static std::string List(const nlohmann::json& params);

    /// Delete a scheduled task
    static std::string Delete(const nlohmann::json& params);

    /// Run a task immediately
    static std::string RunNow(const nlohmann::json& params);

    /// Pause a task
    static std::string Pause(const nlohmann::json& params);

    /// Resume a task
    static std::string Resume(const nlohmann::json& params);
};

}  // namespace aicode
