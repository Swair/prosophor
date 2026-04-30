// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include "common/log_wrapper.h"

#include "common/config.h"
#include "managers/permission_manager.h"
#include "mcp/mcp_client.h"
#include "tools/command_tools/background_run_tool.h"

namespace prosophor {


// struct SkillSchema {
//     std:string type;
//     std::string properties_skill_type;
//     std::string properties_skill_description;
//     std::string properties_args_type;
//     std::string properties_args_description;


// }


/// Tool schema definition for LLM function calling
struct ToolsSchema {
    std::string name;
    std::string description;
    nlohmann::json input_schema;
};

/// Registry for managing and executing tools
class ToolRegistry {
 public:
    static ToolRegistry& GetInstance();

    explicit ToolRegistry();

    /// Register built-in tools
    void RegisterBuiltinTools();

    /// Register a tool with its executor function
    void RegisterTool(const std::string& name, const std::string& description,
                      nlohmann::json parameters,
                      std::function<std::string(const nlohmann::json&)> executor);

    /// Execute a tool by name
    std::string ExecuteTool(const std::string& tool_name,
                            const nlohmann::json& parameters);

    /// Get tool schemas for LLM function calling
    std::vector<ToolsSchema> GetToolSchemas() const;

    /// Check if tool is available
    bool HasTool(const std::string& tool_name) const;

    /// Set workspace root path
    void SetWorkspace(const std::string& path);

    /// Get permission manager reference
    void SetPermissionConfirmCallback(PermissionManager::ConfirmCallback cb);

    /// MCP tool dynamic registration
    void RegisterMcpTools();
    std::vector<std::string> GetRegisteredMcpServers() const;
    bool UnregisterMcpServer(const std::string& server_name);

private:
    std::string GetMcpToolName(const McpTool& tool) const;

 private:
    // ========== File Tools (tools/file_tools/) ==========
    std::string ReadFileTool(const nlohmann::json& params);
    std::string WriteFileTool(const nlohmann::json& params);
    std::string EditFileTool(const nlohmann::json& params);

    // ========== Command Tools ==========
    std::string BashTool(const nlohmann::json& params);
    std::string BackgroundRunTool(const nlohmann::json& params);

    // ========== Web Tools ==========
    std::string WebSearchTool(const nlohmann::json& params);
    std::string WebFetchTool(const nlohmann::json& params);

    // ========== Memory Tools ==========
    std::string MemorySearchTool(const nlohmann::json& params);
    std::string MemoryGetTool(const nlohmann::json& params);

    // ========== Patch Tool ==========
    std::string ApplyPatchTool(const nlohmann::json& params);

    // ========== Token Tools ==========
    std::string TokenCountTool(const nlohmann::json& params);
    std::string TokenUsageTool(const nlohmann::json& params);

    // ========== MCP Tools ==========
    std::string McpListToolsTool(const nlohmann::json& params);
    std::string McpCallToolTool(const nlohmann::json& params);
    std::string McpListResourcesTool(const nlohmann::json& params);
    std::string McpReadResourceTool(const nlohmann::json& params);

    // ========== Git Tools ==========
    std::string GitStatusTool(const nlohmann::json& params);
    std::string GitDiffTool(const nlohmann::json& params);
    std::string GitLogTool(const nlohmann::json& params);
    std::string GitCommitTool(const nlohmann::json& params);
    std::string GitAddTool(const nlohmann::json& params);
    std::string GitBranchTool(const nlohmann::json& params);

    std::unordered_map<std::string,
                       std::function<std::string(const nlohmann::json&)>>
        tools_;
    std::vector<ToolsSchema> tool_schemas_;
    std::string workspace_path_;
};

}  // namespace prosophor
