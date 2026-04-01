// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace aicode {

/// WorktreeTool - Manage git worktrees for task isolation
class WorktreeTool {
public:
    static WorktreeTool& GetInstance();

    /// Execute worktree operations
    std::string Execute(const std::string& action, const nlohmann::json& params);

private:
    WorktreeTool() = default;

    std::string Create(const nlohmann::json& params);
    std::string Remove(const nlohmann::json& params);
    std::string Keep(const nlohmann::json& params);
    std::string Get(const nlohmann::json& params);
    std::string List(const nlohmann::json& params);
    std::string Exec(const nlohmann::json& params);
};

}  // namespace aicode
