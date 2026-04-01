// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace aicode {

/// Agent tool for launching sub-agents and executing skills
class AgentTool {
public:
    /// Execute agent operations
    /// @param action One of: launch, skill, tool_search
    /// @param params Action-specific parameters
    /// @return JSON result or error message
    static std::string Execute(const std::string& action, const nlohmann::json& params);

private:
    /// Launch a sub-agent
    /// @param params {prompt, subagent_type?, model?}
    static std::string Launch(const nlohmann::json& params);

    /// Execute a skill
    /// @param params {skill, args?}
    static std::string ExecuteSkill(const nlohmann::json& params);

    /// Search for available tools
    /// @param params {query}
    static std::string SearchTools(const nlohmann::json& params);
};

}  // namespace aicode
