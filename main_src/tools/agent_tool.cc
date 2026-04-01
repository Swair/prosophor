// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "tools/agent_tool.h"

#include "agents/subagent_coordinator.h"
#include "core/skill_loader.h"
#include "tools/tool_registry.h"
#include "common/log_wrapper.h"

namespace aicode {

std::string AgentTool::Execute(const std::string& action, const nlohmann::json& params) {
    try {
        if (action == "launch") {
            return Launch(params);
        } else if (action == "skill") {
            return ExecuteSkill(params);
        } else if (action == "tool_search") {
            return SearchTools(params);
        } else {
            return "Unknown agent action: " + action;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("AgentTool {} failed: {}", action, e.what());
        return "Error: " + std::string(e.what());
    }
}

std::string AgentTool::Launch(const nlohmann::json& params) {
    auto& coordinator = SubagentCoordinator::GetInstance();

    std::string prompt = params.value("prompt", "");
    if (prompt.empty()) {
        return "Error: 'prompt' is required";
    }

    std::string subagent_type = params.value("subagent_type", "general-purpose");
    std::string model = params.value("model", "");

    LOG_INFO("Launching sub-agent: type={}, model={}", subagent_type, model);

    // Launch the sub-agent
    std::string result = coordinator.LaunchSubagent(prompt, subagent_type, model);

    nlohmann::json response = {
        {"success", true},
        {"subagent_type", subagent_type},
        {"model", model.empty() ? "default" : model},
        {"result", result}
    };

    return response.dump(2);
}

std::string AgentTool::ExecuteSkill(const nlohmann::json& params) {
    std::string skill_name = params.value("skill", "");
    if (skill_name.empty()) {
        return "Error: 'skill' is required";
    }

    std::string args = params.value("args", "");

    LOG_INFO("Executing skill: {}, args={}", skill_name, args.empty() ? "(none)" : args);

    // Load skills from standard directories
    SkillLoader loader;

    // Get workspace path (current directory)
    std::filesystem::path workspace_path = std::filesystem::current_path();

    // Load skills with default config
    SkillsConfig config;
    auto skills = loader.LoadSkills(config, workspace_path);

    // Find the requested skill
    for (const auto& skill : skills) {
        if (skill.name == skill_name) {
            // Build skill execution context
            nlohmann::json result = {
                {"success", true},
                {"skill", skill.name},
                {"description", skill.description},
                {"content", skill.content}
            };

            if (!skill.scripts_dir.empty()) {
                result["scripts_dir"] = skill.scripts_dir;
            }
            if (!skill.references_dir.empty()) {
                result["references_dir"] = skill.references_dir;
            }
            if (!skill.assets_dir.empty()) {
                result["assets_dir"] = skill.assets_dir;
            }

            // Parse args if provided
            if (!args.empty()) {
                try {
                    result["parsed_args"] = nlohmann::json::parse(args);
                } catch (...) {
                    result["raw_args"] = args;
                }
            }

            return result.dump(2);
        }
    }

    // Skill not found - try to install it
    LOG_WARN("Skill '{}' not found, attempting to load from skills directory", skill_name);

    // Try loading from global skills directory
    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : "/tmp";
    std::filesystem::path global_skills_dir = std::filesystem::path(home_str) / ".aicode" / "skills";

    auto global_skills = loader.LoadSkillsFromDirectory(global_skills_dir);
    for (const auto& skill : global_skills) {
        if (skill.name == skill_name) {
            nlohmann::json result = {
                {"success", true},
                {"skill", skill.name},
                {"description", skill.description},
                {"content", skill.content},
                {"message", "Skill loaded from global directory"}
            };
            return result.dump(2);
        }
    }

    // Build available skills list
    std::string available_skills;
    if (skills.empty()) {
        available_skills = "(none)";
    } else {
        for (size_t i = 0; i < skills.size(); ++i) {
            if (i > 0) available_skills += ", ";
            available_skills += skills[i].name;
        }
    }

    return "Error: Skill '" + skill_name + "' not found. Available skills: " + available_skills;
}

std::string AgentTool::SearchTools(const nlohmann::json& params) {
    // Create a temporary registry to access tool schemas
    ToolRegistry registry;
    registry.RegisterBuiltinTools();

    std::string query = params.value("query", "");

    // Get all registered tools
    auto all_tools = registry.GetToolSchemas();

    std::vector<nlohmann::json> matching_tools;

    for (const auto& tool : all_tools) {
        bool matches = false;

        // Search in tool name
        if (query.empty()) {
            matches = true;
        } else if (!tool.name.empty()) {
            if (tool.name.find(query) != std::string::npos) {
                matches = true;
            }
        }

        // Search in description
        if (!matches && !tool.description.empty()) {
            if (tool.description.find(query) != std::string::npos) {
                matches = true;
            }
        }

        // Search in parameters
        if (!matches && !tool.input_schema.empty()) {
            std::string params_str = tool.input_schema.dump();
            if (params_str.find(query) != std::string::npos) {
                matches = true;
            }
        }

        if (matches) {
            nlohmann::json tool_json = {
                {"name", tool.name},
                {"description", tool.description},
                {"input_schema", tool.input_schema}
            };
            matching_tools.push_back(tool_json);
        }
    }

    nlohmann::json response = {
        {"query", query},
        {"tools", matching_tools},
        {"count", static_cast<int>(matching_tools.size())},
        {"total_available", static_cast<int>(all_tools.size())}
    };

    return response.dump(2);
}

}  // namespace aicode
