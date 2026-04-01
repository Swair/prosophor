// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <functional>
#include <nlohmann/json.hpp>

namespace aicode {

/// Task tool for LLM function calling
class TaskTool {
public:
    /// Execute task-related operations
    /// @param action One of: create, get, update, list, delete
    /// @param params Action-specific parameters
    /// @return JSON result or error message
    static std::string Execute(const std::string& action, const nlohmann::json& params);

private:
    /// Create a new task
    static std::string CreateTask(const nlohmann::json& params);

    /// Get task by ID
    static std::string GetTask(const nlohmann::json& params);

    /// Update task
    static std::string UpdateTask(const nlohmann::json& params);

    /// List all tasks
    static std::string ListTasks(const nlohmann::json& params);

    /// Delete a task
    static std::string DeleteTask(const nlohmann::json& params);

    /// Get task output
    static std::string GetTaskOutput(const nlohmann::json& params);

    /// Claim an unclaimed task (for autonomous agent)
    static std::string ClaimTask(const nlohmann::json& params);

    /// Scan for unclaimed tasks
    static std::string ScanUnclaimedTasks(const nlohmann::json& params);
};

}  // namespace aicode
