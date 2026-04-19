// Copyright 2026 AiCode Contributors
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
#include "tools/agent_tools/agent_tool.h"
#include "tools/command_tools/background_run_tool.h"
#include "tools/interaction_tools/ask_user_question_tool.h"
#include "tools/lsp_tools/lsp_tool.h"
#include "tools/search_tools/glob_tool.h"
#include "tools/search_tools/grep_tool.h"
#include "tools/task_tools/cron_tool.h"
#include "tools/task_tools/task_tool.h"
#include "tools/task_tools/todo_write_tool.h"
#include "tools/worktree_tools/worktree_tool.h"
#include "mcp/mcp_client.h"

namespace aicode {


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

    // ========== Command Tools (tools/command_tools/) ==========
    std::string BashTool(const nlohmann::json& params);
    std::string ExecTool(const nlohmann::json& params);

    // ========== Search Tools (tools/search_tools/) ==========
    std::string GlobTool(const nlohmann::json& params);
    std::string GrepTool(const nlohmann::json& params);
    std::string WebSearchTool(const nlohmann::json& params);
    std::string WebFetchTool(const nlohmann::json& params);

    // ========== Token Tools ==========
    std::string TokenCountTool(const nlohmann::json& params);
    std::string TokenUsageTool(const nlohmann::json& params);

    // ========== MCP Tools ==========
    std::string McpListToolsTool(const nlohmann::json& params);
    std::string McpCallToolTool(const nlohmann::json& params);
    std::string McpListResourcesTool(const nlohmann::json& params);
    std::string McpReadResourceTool(const nlohmann::json& params);

    // ========== Git Tools (tools/git_tools/) ==========
    std::string GitStatusTool(const nlohmann::json& params);
    std::string GitDiffTool(const nlohmann::json& params);
    std::string GitLogTool(const nlohmann::json& params);
    std::string GitCommitTool(const nlohmann::json& params);
    std::string GitAddTool(const nlohmann::json& params);
    std::string GitBranchTool(const nlohmann::json& params);

    // ========== Interaction Tools (tools/interaction_tools/) ==========
    std::string AskUserQuestionTool(const nlohmann::json& params);

    // ========== Task Tools (tools/task_tools/) ==========
    std::string TodoWriteTool(const nlohmann::json& params);

    // ========== Worktree Tools (tools/worktree_tools/) ==========
    std::string WorktreeTool(const nlohmann::json& params);

    // ========== Background Task Tools (tools/command_tools/) ==========
    std::string BackgroundRunTool(const nlohmann::json& params);

    // ========== LSP Tools (tools/lsp_tools/) ==========
    std::string LspDiagnosticsTool(const nlohmann::json& params);
    std::string LspGoToDefinitionTool(const nlohmann::json& params);
    std::string LspFindReferencesTool(const nlohmann::json& params);
    std::string LspGetHoverTool(const nlohmann::json& params);
    std::string LspDocumentSymbolsTool(const nlohmann::json& params);
    std::string LspWorkspaceSymbolsTool(const nlohmann::json& params);
    std::string LspFormatDocumentTool(const nlohmann::json& params);
    std::string LspListServersTool(const nlohmann::json& params);
    std::string LspAllDiagnosticsTool(const nlohmann::json& params);

    // ========== Agent Tools (tools/agent_tools/) ==========
    // Note: AgentTool uses Execute() static method, not declared here

    std::unordered_map<std::string,
                       std::function<std::string(const nlohmann::json&)>>
        tools_;
    std::vector<ToolsSchema> tool_schemas_;
    std::string workspace_path_;
};

}  // namespace aicode
