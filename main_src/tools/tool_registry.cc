// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "tools/tool_registry.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>

#include "common/log_wrapper.h"
#include "common/curl_client.h"
#include "managers/token_tracker.h"
#include "managers/permission_manager.h"
#include "mcp/mcp_client.h"
#include "tools/lsp_tool.h"
#include "tools/task_tool.h"
#include "tools/cron_tool.h"
#include "tools/agent_tool.h"
#include "tools/glob_tool.h"
#include "tools/grep_tool.h"
#include "tools/worktree_tool.h"
#include "tools/background_run_tool.h"

namespace aicode {

ToolRegistry::ToolRegistry()
    : workspace_path_("~/.aicode/workspace") {
    LOG_INFO("ToolRegistry initialized");

    // Initialize permission manager with default config
    auto& perm_manager = PermissionManager::GetInstance();
    perm_manager.Initialize();
}

void ToolRegistry::RegisterBuiltinTools() {
    // read_file
    {
        nlohmann::json params = nlohmann::json::object();
        params["path"] = nlohmann::json::object();
        params["path"]["type"] = "string";
        RegisterTool("read_file", "Read content of a file", params,
                     [this](const nlohmann::json& p) { return ReadFileTool(p); });
    }

    // write_file
    {
        nlohmann::json params = nlohmann::json::object();
        params["path"] = nlohmann::json::object();
        params["path"]["type"] = "string";
        params["content"] = nlohmann::json::object();
        params["content"]["type"] = "string";
        RegisterTool("write_file", "Write content to a file", params,
                     [this](const nlohmann::json& p) { return WriteFileTool(p); });
    }

    // edit_file
    {
        nlohmann::json params = nlohmann::json::object();
        params["path"] = nlohmann::json::object();
        params["path"]["type"] = "string";
        params["old_text"] = nlohmann::json::object();
        params["old_text"]["type"] = "string";
        params["new_text"] = nlohmann::json::object();
        params["new_text"]["type"] = "string";
        RegisterTool("edit_file", "Edit a file with search/replace", params,
                     [this](const nlohmann::json& p) { return EditFileTool(p); });
    }

    // bash
    {
        nlohmann::json params = nlohmann::json::object();
        params["command"] = nlohmann::json::object();
        params["command"]["type"] = "string";
        RegisterTool("bash", "Execute a bash command", params,
                     [this](const nlohmann::json& p) { return BashTool(p); });
    }

    // glob
    {
        nlohmann::json params = nlohmann::json::object();
        params["pattern"] = nlohmann::json::object();
        params["pattern"]["type"] = "string";
        RegisterTool("glob", "Find files by pattern", params,
                     [this](const nlohmann::json& p) { return GlobTool(p); });
    }

    // grep
    {
        nlohmann::json params = nlohmann::json::object();
        params["pattern"] = nlohmann::json::object();
        params["pattern"]["type"] = "string";
        params["path"] = nlohmann::json::object();
        params["path"]["type"] = "string";
        RegisterTool("grep", "Search for a pattern in files", params,
                     [this](const nlohmann::json& p) { return GrepTool(p); });
    }

    // web_search
    {
        nlohmann::json params = nlohmann::json::object();
        params["query"] = nlohmann::json::object();
        params["query"]["type"] = "string";
        RegisterTool("web_search", "Search the web", params,
                     [this](const nlohmann::json& p) { return WebSearchTool(p); });
    }

    // web_fetch
    {
        nlohmann::json params = nlohmann::json::object();
        params["url"] = nlohmann::json::object();
        params["url"]["type"] = "string";
        RegisterTool("web_fetch", "Fetch content from a URL", params,
                     [this](const nlohmann::json& p) { return WebFetchTool(p); });
    }

    // token_count - Count tokens in text
    {
        nlohmann::json params = nlohmann::json::object();
        params["text"] = nlohmann::json::object();
        params["text"]["type"] = "string";
        params["model"] = nlohmann::json::object();
        params["model"]["type"] = "string";
        params["model"]["description"] = "Model name for token counting";
        RegisterTool("token_count", "Count tokens in text", params,
                     [this](const nlohmann::json& p) { return TokenCountTool(p); });
    }

    // token_usage - Get token usage statistics
    {
        nlohmann::json params = nlohmann::json::object();
        params["model"] = nlohmann::json::object();
        params["model"]["type"] = "string";
        params["model"]["description"] = "Model name (optional, omit for total)";
        RegisterTool("token_usage", "Get token usage statistics", params,
                     [this](const nlohmann::json& p) { return TokenUsageTool(p); });
    }

    // mcp_list_tools - List available MCP tools
    {
        nlohmann::json params = nlohmann::json::object();
        params["server"] = nlohmann::json::object();
        params["server"]["type"] = "string";
        params["server"]["description"] = "Server name (optional, omit for all)";
        RegisterTool("mcp_list_tools", "List available MCP tools", params,
                     [this](const nlohmann::json& p) { return McpListToolsTool(p); });
    }

    // mcp_call_tool - Call an MCP tool
    {
        nlohmann::json params = nlohmann::json::object();
        params["tool_name"] = nlohmann::json::object();
        params["tool_name"]["type"] = "string";
        params["tool_name"]["description"] = "Name of the tool to call";
        params["arguments"] = nlohmann::json::object();
        params["arguments"]["type"] = "object";
        params["arguments"]["description"] = "Arguments to pass to the tool";
        RegisterTool("mcp_call_tool", "Call an MCP tool", params,
                     [this](const nlohmann::json& p) { return McpCallToolTool(p); });
    }

    // mcp_list_resources - List available MCP resources
    {
        nlohmann::json params = nlohmann::json::object();
        RegisterTool("mcp_list_resources", "List available MCP resources", params,
                     [this](const nlohmann::json& p) { return McpListResourcesTool(p); });
    }

    // mcp_read_resource - Read an MCP resource
    {
        nlohmann::json params = nlohmann::json::object();
        params["uri"] = nlohmann::json::object();
        params["uri"]["type"] = "string";
        params["uri"]["description"] = "URI of the resource to read";
        RegisterTool("mcp_read_resource", "Read an MCP resource", params,
                     [this](const nlohmann::json& p) { return McpReadResourceTool(p); });
    }

    // git_status - Show git repository status
    {
        nlohmann::json params = nlohmann::json::object();
        params["path"] = nlohmann::json::object();
        params["path"]["type"] = "string";
        params["path"]["description"] = "Repository path (default: workspace)";
        RegisterTool("git_status", "Show git repository status", params,
                     [this](const nlohmann::json& p) { return GitStatusTool(p); });
    }

    // git_diff - Show changes in git repository
    {
        nlohmann::json params = nlohmann::json::object();
        params["path"] = nlohmann::json::object();
        params["path"]["type"] = "string";
        params["path"]["description"] = "Repository path (default: workspace)";
        params["cached"] = nlohmann::json::object();
        params["cached"]["type"] = "boolean";
        params["cached"]["description"] = "Show staged changes (default: false)";
        RegisterTool("git_diff", "Show git changes", params,
                     [this](const nlohmann::json& p) { return GitDiffTool(p); });
    }

    // git_log - Show git commit history
    {
        nlohmann::json params = nlohmann::json::object();
        params["path"] = nlohmann::json::object();
        params["path"]["type"] = "string";
        params["path"]["description"] = "Repository path (default: workspace)";
        params["max_count"] = nlohmann::json::object();
        params["max_count"]["type"] = "integer";
        params["max_count"]["description"] = "Maximum number of commits to show (default: 10)";
        RegisterTool("git_log", "Show git commit history", params,
                     [this](const nlohmann::json& p) { return GitLogTool(p); });
    }

    // git_commit - Create a git commit
    {
        nlohmann::json params = nlohmann::json::object();
        params["message"] = nlohmann::json::object();
        params["message"]["type"] = "string";
        params["message"]["description"] = "Commit message";
        params["all"] = nlohmann::json::object();
        params["all"]["type"] = "boolean";
        params["all"]["description"] = "Auto-stage all changes before commit (default: false)";
        RegisterTool("git_commit", "Create a git commit", params,
                     [this](const nlohmann::json& p) { return GitCommitTool(p); });
    }

    // git_add - Stage file changes
    {
        nlohmann::json params = nlohmann::json::object();
        params["files"] = nlohmann::json::object();
        params["files"]["type"] = "array";
        params["files"]["items"] = {{"type", "string"}};
        params["files"]["description"] = "Files to stage (empty = stage all)";
        RegisterTool("git_add", "Stage file changes for commit", params,
                     [this](const nlohmann::json& p) { return GitAddTool(p); });
    }

    // git_branch - List or create git branches
    {
        nlohmann::json params = nlohmann::json::object();
        params["path"] = nlohmann::json::object();
        params["path"]["type"] = "string";
        params["path"]["description"] = "Repository path (default: workspace)";
        params["create"] = nlohmann::json::object();
        params["create"]["type"] = "string";
        params["create"]["description"] = "Branch name to create";
        params["checkout"] = nlohmann::json::object();
        params["checkout"]["type"] = "boolean";
        params["checkout"]["description"] = "Checkout the branch after creating (default: false)";
        RegisterTool("git_branch", "List or create git branches", params,
                     [this](const nlohmann::json& p) { return GitBranchTool(p); });
    }

    // ask_user_question - Interactive user questions
    {
        nlohmann::json params = nlohmann::json::object();
        params["questions"] = nlohmann::json::object();
        params["questions"]["type"] = "array";
        params["questions"]["description"] = "List of questions to ask (1-4)";
        params["questions"]["items"] = nlohmann::json::object();
        params["questions"]["items"]["type"] = "object";
        params["questions"]["items"]["properties"] = nlohmann::json::object();
        params["questions"]["items"]["properties"]["question"] = nlohmann::json::object();
        params["questions"]["items"]["properties"]["question"]["type"] = "string";
        params["questions"]["items"]["properties"]["header"] = nlohmann::json::object();
        params["questions"]["items"]["properties"]["header"]["type"] = "string";
        params["questions"]["items"]["properties"]["options"] = nlohmann::json::object();
        params["questions"]["items"]["properties"]["options"]["type"] = "array";
        params["questions"]["items"]["properties"]["multiSelect"] = nlohmann::json::object();
        params["questions"]["items"]["properties"]["multiSelect"]["type"] = "boolean";
        RegisterTool("ask_user_question", "Ask the user interactive questions with multiple choice options", params,
                     [this](const nlohmann::json& p) { return this->AskUserQuestionTool(p); });
    }

    // todo_write - TODO list management
    {
        nlohmann::json params = nlohmann::json::object();
        params["operation"] = nlohmann::json::object();
        params["operation"]["type"] = "string";
        params["operation"]["description"] = "Operation: create, update, delete, list, clear";
        params["id"] = nlohmann::json::object();
        params["id"]["type"] = "string";
        params["id"]["description"] = "Todo item ID (for update/delete)";
        params["content"] = nlohmann::json::object();
        params["content"]["type"] = "string";
        params["content"]["description"] = "Todo content (for create)";
        params["status"] = nlohmann::json::object();
        params["status"]["type"] = "string";
        params["status"]["description"] = "Status: pending, completed, cancelled (for update)";
        params["priority"] = nlohmann::json::object();
        params["priority"]["type"] = "string";
        params["priority"]["description"] = "Priority: low, medium, high (for create)";
        RegisterTool("todo_write", "Manage TODO list items", params,
                     [this](const nlohmann::json& p) { return this->TodoWriteTool(p); });
    }

    // Task management tools
    {
        nlohmann::json params = nlohmann::json::object();
        params["subject"] = nlohmann::json::object();
        params["subject"]["type"] = "string";
        params["subject"]["description"] = "Task subject";
        params["description"] = nlohmann::json::object();
        params["description"]["type"] = "string";
        params["description"]["description"] = "Task description";
        params["active_form"] = nlohmann::json::object();
        params["active_form"]["type"] = "string";
        params["active_form"]["description"] = "Present continuous form for spinner display";
        RegisterTool("task_create", "Create a new task with subject and description", params,
                     [](const nlohmann::json& p) { return TaskTool::Execute("create", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["task_id"] = nlohmann::json::object();
        params["task_id"]["type"] = "string";
        params["task_id"]["description"] = "Task ID";
        RegisterTool("task_get", "Get task details by ID", params,
                     [](const nlohmann::json& p) { return TaskTool::Execute("get", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["task_id"] = nlohmann::json::object();
        params["task_id"]["type"] = "string";
        params["task_id"]["description"] = "Task ID";
        params["status"] = nlohmann::json::object();
        params["status"]["type"] = "string";
        params["status"]["description"] = "New status: pending, in_progress, completed, cancelled, failed";
        params["owner"] = nlohmann::json::object();
        params["owner"]["type"] = "string";
        params["owner"]["description"] = "Task owner/assignee";
        RegisterTool("task_update", "Update task status, owner, or description", params,
                     [](const nlohmann::json& p) { return TaskTool::Execute("update", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["status"] = nlohmann::json::object();
        params["status"]["type"] = "string";
        params["status"]["description"] = "Filter by status (optional)";
        RegisterTool("task_list", "List all tasks, optionally filtered by status", params,
                     [](const nlohmann::json& p) { return TaskTool::Execute("list", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["task_id"] = nlohmann::json::object();
        params["task_id"]["type"] = "string";
        params["task_id"]["description"] = "Task ID to delete";
        RegisterTool("task_delete", "Delete a task by ID", params,
                     [](const nlohmann::json& p) { return TaskTool::Execute("delete", p); });
    }

    // Cron scheduled task tools
    {
        nlohmann::json params = nlohmann::json::object();
        params["cron"] = nlohmann::json::object();
        params["cron"]["type"] = "string";
        params["cron"]["description"] = "Cron expression (5-field: M H DoM Mon DoW)";
        params["prompt"] = nlohmann::json::object();
        params["prompt"]["type"] = "string";
        params["prompt"]["description"] = "Prompt to execute";
        params["recurring"] = nlohmann::json::object();
        params["recurring"]["type"] = "boolean";
        params["recurring"]["description"] = "Whether to repeat (default true)";
        params["durable"] = nlohmann::json::object();
        params["durable"]["type"] = "boolean";
        params["durable"]["description"] = "Persist to file (default false)";
        RegisterTool("cron_create", "Schedule a new recurring or one-shot task", params,
                     [](const nlohmann::json& p) { return CronTool::Execute("create", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        RegisterTool("cron_list", "List all scheduled tasks", params,
                     [](const nlohmann::json& p) { return CronTool::Execute("list", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["task_id"] = nlohmann::json::object();
        params["task_id"]["type"] = "string";
        params["task_id"]["description"] = "Task ID to delete";
        RegisterTool("cron_delete", "Delete a scheduled task", params,
                     [](const nlohmann::json& p) { return CronTool::Execute("delete", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["task_id"] = nlohmann::json::object();
        params["task_id"]["type"] = "string";
        params["task_id"]["description"] = "Task ID to run immediately";
        RegisterTool("cron_run", "Run a scheduled task immediately", params,
                     [](const nlohmann::json& p) { return CronTool::Execute("run", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["task_id"] = nlohmann::json::object();
        params["task_id"]["type"] = "string";
        params["task_id"]["description"] = "Task ID to pause";
        RegisterTool("cron_pause", "Pause a scheduled task", params,
                     [](const nlohmann::json& p) { return CronTool::Execute("pause", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["task_id"] = nlohmann::json::object();
        params["task_id"]["type"] = "string";
        params["task_id"]["description"] = "Task ID to resume";
        RegisterTool("cron_resume", "Resume a paused scheduled task", params,
                     [](const nlohmann::json& p) { return CronTool::Execute("resume", p); });
    }

    // LSP tools - Language Server Protocol integration
    {
        nlohmann::json params = nlohmann::json::object();
        params["uri"] = nlohmann::json::object();
        params["uri"]["type"] = "string";
        params["uri"]["description"] = "File URI (file://path/to/file)";
        params["severity"] = nlohmann::json::object();
        params["severity"]["type"] = "string";
        params["severity"]["description"] = "Filter by severity: error|warning|information|hint";
        RegisterTool("lsp_diagnostics", "Get diagnostics (errors, warnings) for a file", params,
                     [this](const nlohmann::json& p) { return this->LspDiagnosticsTool(p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["uri"] = nlohmann::json::object();
        params["uri"]["type"] = "string";
        params["uri"]["description"] = "File URI (file://path/to/file)";
        params["line"] = nlohmann::json::object();
        params["line"]["type"] = "integer";
        params["line"]["description"] = "Line number (0-based)";
        params["character"] = nlohmann::json::object();
        params["character"]["type"] = "integer";
        params["character"]["description"] = "Character position (0-based)";
        RegisterTool("lsp_go_to_definition", "Go to definition of symbol at position", params,
                     [this](const nlohmann::json& p) { return this->LspGoToDefinitionTool(p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["uri"] = nlohmann::json::object();
        params["uri"]["type"] = "string";
        params["uri"]["description"] = "File URI";
        params["line"] = nlohmann::json::object();
        params["line"]["type"] = "integer";
        params["line"]["description"] = "Line number (0-based)";
        params["character"] = nlohmann::json::object();
        params["character"]["type"] = "integer";
        params["character"]["description"] = "Character position (0-based)";
        params["include_declaration"] = nlohmann::json::object();
        params["include_declaration"]["type"] = "boolean";
        params["include_declaration"]["description"] = "Include declaration in results";
        RegisterTool("lsp_find_references", "Find all references to symbol at position", params,
                     [this](const nlohmann::json& p) { return this->LspFindReferencesTool(p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["uri"] = nlohmann::json::object();
        params["uri"]["type"] = "string";
        params["uri"]["description"] = "File URI";
        params["line"] = nlohmann::json::object();
        params["line"]["type"] = "integer";
        params["line"]["description"] = "Line number (0-based)";
        params["character"] = nlohmann::json::object();
        params["character"]["type"] = "integer";
        params["character"]["description"] = "Character position (0-based)";
        RegisterTool("lsp_get_hover", "Get hover information (type, docs) for symbol at position", params,
                     [this](const nlohmann::json& p) { return this->LspGetHoverTool(p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["uri"] = nlohmann::json::object();
        params["uri"]["type"] = "string";
        params["uri"]["description"] = "File URI";
        RegisterTool("lsp_document_symbols", "Get all symbols (functions, classes, etc.) in document", params,
                     [this](const nlohmann::json& p) { return this->LspDocumentSymbolsTool(p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["query"] = nlohmann::json::object();
        params["query"]["type"] = "string";
        params["query"]["description"] = "Symbol search query";
        RegisterTool("lsp_workspace_symbols", "Search for symbols across entire workspace", params,
                     [this](const nlohmann::json& p) { return this->LspWorkspaceSymbolsTool(p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["uri"] = nlohmann::json::object();
        params["uri"]["type"] = "string";
        params["uri"]["description"] = "File URI";
        RegisterTool("lsp_format_document", "Format document according to language rules", params,
                     [this](const nlohmann::json& p) { return this->LspFormatDocumentTool(p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        RegisterTool("lsp_all_diagnostics", "Get all diagnostics across all open files", params,
                     [this](const nlohmann::json& p) { return this->LspAllDiagnosticsTool(p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        RegisterTool("lsp_list_servers", "List all registered LSP servers", params,
                     [this](const nlohmann::json& p) { return this->LspListServersTool(p); });
    }

    // Agent tools - Sub-agent launching and skill execution
    {
        nlohmann::json params = nlohmann::json::object();
        params["prompt"] = nlohmann::json::object();
        params["prompt"]["type"] = "string";
        params["prompt"]["description"] = "Task description for the sub-agent";
        params["subagent_type"] = nlohmann::json::object();
        params["subagent_type"]["type"] = "string";
        params["subagent_type"]["description"] = "Type: general-purpose, Explore, Plan, Code (optional)";
        params["model"] = nlohmann::json::object();
        params["model"]["type"] = "string";
        params["model"]["description"] = "Model to use (optional)";
        RegisterTool("agent_launch", "Launch a sub-agent to handle a complex task", params,
                     [](const nlohmann::json& p) { return AgentTool::Execute("launch", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["skill"] = nlohmann::json::object();
        params["skill"]["type"] = "string";
        params["skill"]["description"] = "Name of the skill to execute";
        params["args"] = nlohmann::json::object();
        params["args"]["type"] = "string";
        params["args"]["description"] = "Arguments for the skill (optional)";
        RegisterTool("agent_skill", "Execute a skill", params,
                     [](const nlohmann::json& p) { return AgentTool::Execute("skill", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["query"] = nlohmann::json::object();
        params["query"]["type"] = "string";
        params["query"]["description"] = "Search query to find tools";
        RegisterTool("agent_tool_search", "Search for available tools by name or description", params,
                     [](const nlohmann::json& p) { return AgentTool::Execute("tool_search", p); });
    }

    // Worktree tools - git worktree management for task isolation
    {
        nlohmann::json params = nlohmann::json::object();
        params["name"] = nlohmann::json::object();
        params["name"]["type"] = "string";
        params["name"]["description"] = "Worktree name";
        params["task_id"] = nlohmann::json::object();
        params["task_id"]["type"] = "string";
        params["task_id"]["description"] = "Associated task ID (optional)";
        params["base_branch"] = nlohmann::json::object();
        params["base_branch"]["type"] = "string";
        params["base_branch"]["description"] = "Base branch to create from (optional)";
        RegisterTool("worktree_create", "Create a new git worktree for task isolation", params,
                     [](const nlohmann::json& p) { return WorktreeTool::GetInstance().Execute("create", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["name"] = nlohmann::json::object();
        params["name"]["type"] = "string";
        params["name"]["description"] = "Worktree name";
        params["force"] = nlohmann::json::object();
        params["force"]["type"] = "boolean";
        params["force"]["description"] = "Force removal (default: false)";
        params["complete_task"] = nlohmann::json::object();
        params["complete_task"]["type"] = "boolean";
        params["complete_task"]["description"] = "Complete associated task (default: false)";
        RegisterTool("worktree_remove", "Remove a git worktree", params,
                     [](const nlohmann::json& p) { return WorktreeTool::GetInstance().Execute("remove", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["name"] = nlohmann::json::object();
        params["name"]["type"] = "string";
        params["name"]["description"] = "Worktree name";
        RegisterTool("worktree_keep", "Keep a worktree (mark as persistent)", params,
                     [](const nlohmann::json& p) { return WorktreeTool::GetInstance().Execute("keep", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["name"] = nlohmann::json::object();
        params["name"]["type"] = "string";
        params["name"]["description"] = "Worktree name";
        RegisterTool("worktree_get", "Get worktree details", params,
                     [](const nlohmann::json& p) { return WorktreeTool::GetInstance().Execute("get", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        RegisterTool("worktree_list", "List all worktrees", params,
                     [](const nlohmann::json& p) { return WorktreeTool::GetInstance().Execute("list", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["worktree"] = nlohmann::json::object();
        params["worktree"]["type"] = "string";
        params["worktree"]["description"] = "Worktree name";
        params["command"] = nlohmann::json::object();
        params["command"]["type"] = "string";
        params["command"]["description"] = "Command to execute";
        params["timeout"] = nlohmann::json::object();
        params["timeout"]["type"] = "integer";
        params["timeout"]["description"] = "Timeout in seconds (default: 300)";
        RegisterTool("worktree_exec", "Execute a command in a worktree", params,
                     [](const nlohmann::json& p) { return WorktreeTool::GetInstance().Execute("exec", p); });
    }

    // Background run tools - async command execution
    {
        nlohmann::json params = nlohmann::json::object();
        params["command"] = nlohmann::json::object();
        params["command"]["type"] = "string";
        params["command"]["description"] = "Shell command to run in background";
        params["cwd"] = nlohmann::json::object();
        params["cwd"]["type"] = "string";
        params["cwd"]["description"] = "Working directory (optional)";
        RegisterTool("background_run", "Run a command in background", params,
                     [](const nlohmann::json& p) { return BackgroundRunTool::GetInstance().Execute("run", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["task_id"] = nlohmann::json::object();
        params["task_id"]["type"] = "string";
        params["task_id"]["description"] = "Background task ID";
        RegisterTool("background_get", "Get background task status and result", params,
                     [](const nlohmann::json& p) { return BackgroundRunTool::GetInstance().Execute("get", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        RegisterTool("background_list", "List all background tasks", params,
                     [](const nlohmann::json& p) { return BackgroundRunTool::GetInstance().Execute("list", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        params["task_id"] = nlohmann::json::object();
        params["task_id"]["type"] = "string";
        params["task_id"]["description"] = "Background task ID";
        RegisterTool("background_cancel", "Cancel a background task", params,
                     [](const nlohmann::json& p) { return BackgroundRunTool::GetInstance().Execute("cancel", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        RegisterTool("background_drain", "Drain background task notifications", params,
                     [](const nlohmann::json& p) { return BackgroundRunTool::GetInstance().Execute("drain", p); });
    }

    // Task claim and scan tools for autonomous agents
    {
        nlohmann::json params = nlohmann::json::object();
        params["task_id"] = nlohmann::json::object();
        params["task_id"]["type"] = "string";
        params["task_id"]["description"] = "Task ID to claim";
        params["agent_id"] = nlohmann::json::object();
        params["agent_id"]["type"] = "string";
        params["agent_id"]["description"] = "Agent ID claiming the task";
        RegisterTool("task_claim", "Claim an unclaimed task", params,
                     [](const nlohmann::json& p) { return TaskTool::Execute("claim", p); });
    }

    {
        nlohmann::json params = nlohmann::json::object();
        RegisterTool("task_scan_unclaimed", "Scan for unclaimed tasks", params,
                     [](const nlohmann::json& p) { return TaskTool::Execute("scan_unclaimed", p); });
    }

    // Register MCP tools dynamically
    RegisterMcpTools();

    LOG_INFO("Registered {} built-in tools", tool_schemas_.size());
}

void ToolRegistry::RegisterTool(
    const std::string& name, const std::string& description,
    nlohmann::json parameters,
    std::function<std::string(const nlohmann::json&)> executor) {
    tools_[name] = executor;

    ToolsSchema schema;
    schema.name = name;
    schema.description = description;

    // Wrap parameters in OpenAI-compatible function schema format
    nlohmann::json wrapped_schema = nlohmann::json::object();
    wrapped_schema["type"] = "object";
    wrapped_schema["properties"] = std::move(parameters);

    // Extract required fields (all defined properties are required by default)
    nlohmann::json required = nlohmann::json::array();
    for (const auto& [key, value] : wrapped_schema["properties"].items()) {
        required.push_back(key);
    }
    wrapped_schema["required"] = required;

    schema.input_schema = std::move(wrapped_schema);
    tool_schemas_.push_back(schema);

    LOG_DEBUG("Registered tool: {}", name);
}

std::vector<ToolsSchema> ToolRegistry::GetToolSchemas() const {
    return tool_schemas_;
}

bool ToolRegistry::HasTool(const std::string& tool_name) const {
    return tools_.find(tool_name) != tools_.end();
}

void ToolRegistry::SetWorkspace(const std::string& path) {
    workspace_path_ = path;
    LOG_INFO("Set workspace: {}", workspace_path_);
}

void ToolRegistry::SetPermissionConfirmCallback(PermissionManager::ConfirmCallback cb) {
    auto& perm_manager = PermissionManager::GetInstance();
    perm_manager.SetConfirmCallback(std::move(cb));
    LOG_INFO("Permission confirmation callback set");
}

std::string ToolRegistry::ExecuteTool(const std::string& tool_name,
                                      const nlohmann::json& parameters) {
    auto it = tools_.find(tool_name);
    if (it == tools_.end()) {
        throw std::runtime_error("Tool not found: " + tool_name);
    }

    // Check permissions before executing tool
    auto& perm_manager = PermissionManager::GetInstance();
    auto perm_result = perm_manager.CheckPermission(tool_name, parameters);

    if (perm_result.level == PermissionLevel::Deny) {
        LOG_WARN("Tool {} denied: {}", tool_name, perm_result.reason);
        throw std::runtime_error("Tool execution denied: " + perm_result.reason);
    }

    if (perm_result.level == PermissionLevel::Ask) {
        // Request user confirmation
        if (!perm_manager.RequestUserConfirmation(tool_name, parameters, perm_result.reason)) {
            LOG_WARN("Tool {} confirmation denied by user", tool_name);
            throw std::runtime_error("Tool execution denied by user");
        }
    }

    LOG_DEBUG("Executing tool: {}", tool_name);
    return it->second(parameters);
}

std::string ToolRegistry::ReadFileTool(const nlohmann::json& params) {
    std::string path = params.value("path", "");
    if (path.empty()) {
        throw std::runtime_error("Path is required");
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }

    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

std::string ToolRegistry::WriteFileTool(const nlohmann::json& params) {
    std::string path = params.value("path", "");
    std::string content = params.value("content", "");

    if (path.empty()) {
        throw std::runtime_error("Path is required");
    }

    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot write to file: " + path);
    }

    file << content;
    return "File written successfully: " + path;
}

std::string ToolRegistry::EditFileTool(const nlohmann::json& params) {
    std::string path = params.value("path", "");
    std::string old_text = params.value("old_text", "");
    std::string new_text = params.value("new_text", "");

    if (path.empty() || old_text.empty()) {
        throw std::runtime_error("Path and old_text are required");
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    size_t pos = content.find(old_text);
    if (pos == std::string::npos) {
        throw std::runtime_error("Old text not found in file");
    }

    content.replace(pos, old_text.length(), new_text);

    std::ofstream out_file(path);
    if (!out_file.is_open()) {
        throw std::runtime_error("Cannot write to file: " + path);
    }

    out_file << content;
    return "File edited successfully: " + path;
}

std::string ToolRegistry::BashTool(const nlohmann::json& params) {
    std::string command = params.value("command", "");
    if (command.empty()) {
        throw std::runtime_error("Command is required");
    }

    std::string result;
    std::string cmd = command + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to execute command");
    }

    // Use larger buffer to avoid truncating error messages
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }

    int status = pclose(pipe);

    // Always include exit code information
    std::string exit_info = "\n[Exit code: " + std::to_string(status) + "]";

    if (status != 0) {
        // Command failed - throw exception so full error is passed to LLM without truncation
        result += exit_info;
        throw std::runtime_error(result);
    }

    // Command succeeded - truncate if needed
    result += exit_info;
    return result;
}

std::string ToolRegistry::GlobTool(const nlohmann::json& params) {
    return GlobTool::GetInstance().Execute(params);
}

std::string ToolRegistry::GrepTool(const nlohmann::json& params) {
    return GrepTool::GetInstance().Execute(params);
}

std::string ToolRegistry::WebFetchTool(const nlohmann::json& params) {
    std::string url = params.value("url", "");
    if (url.empty()) {
        throw std::runtime_error("URL is required");
    }

    // Validate URL scheme
    if (url.find("http://") != 0 && url.find("https://") != 0) {
        return "Error: Invalid URL scheme. URL must start with http:// or https://";
    }

    // Use curl to fetch the URL
    HttpRequest req;
    req.url = url;
    req.timeout_seconds = 30;
    req.user_agent = "AiCode/1.0 (WebFetch Tool)";

    HttpResponse resp = HttpClient::Post(req);

    if (resp.failed()) {
        return "Error fetching URL: " + resp.error + " (HTTP " + std::to_string(resp.status_code) + ")";
    }

    // Simple HTML to text conversion - strip tags
    std::string text;
    bool in_tag = false;
    for (char c : resp.body) {
        if (c == '<') {
            in_tag = true;
        } else if (c == '>') {
            in_tag = false;
        } else if (!in_tag) {
            text += c;
        }
    }

    // Clean up whitespace
    std::string result;
    bool last_was_space = true;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!last_was_space) {
                result += ' ';
                last_was_space = true;
            }
        } else {
            result += c;
            last_was_space = false;
        }
    }

    // Limit output size
    const size_t MAX_OUTPUT = 50000;
    if (result.size() > MAX_OUTPUT) {
        result = result.substr(0, MAX_OUTPUT) + "\n\n[Content truncated...]";
    }

    return result;
}

std::string ToolRegistry::WebSearchTool(const nlohmann::json& params) {
    std::string query = params.value("query", "");
    if (query.empty()) {
        throw std::runtime_error("Query is required");
    }

    // Use DuckDuckGoo HTML search (no API key required for basic usage)
    std::string encoded_query;
    for (char c : query) {
        if (c == ' ') {
            encoded_query += '+';
        } else if (c == '"' || c == '\'' || c == '<' || c == '>') {
            encoded_query += '%' + std::to_string(static_cast<unsigned char>(c));
        } else {
            encoded_query += c;
        }
    }

    std::string url = "https://html.duckduckgo.com/html/?q=" + encoded_query;

    HttpRequest req;
    req.url = url;
    req.timeout_seconds = 30;
    req.user_agent = "Mozilla/5.0 (compatible; AiCode/1.0)";

    HttpResponse resp = HttpClient::Post(req);

    if (resp.failed()) {
        return "Error performing search: " + resp.error + " (HTTP " + std::to_string(resp.status_code) + ")";
    }

    // Parse DuckDuckGoo HTML results
    std::ostringstream result;
    result << "Search results for: " << query << "\n\n";

    // Simple extraction of result URLs and titles
    size_t pos = 0;
    int result_count = 0;
    const std::string result_marker = "result__a";

    while ((pos = resp.body.find(result_marker, pos)) != std::string::npos && result_count < 10) {
        // Find URL
        size_t url_start = resp.body.find("href=\"", pos);
        if (url_start != std::string::npos) {
            url_start += 6;
            size_t url_end = resp.body.find("\"", url_start);
            if (url_end != std::string::npos) {
                std::string url = resp.body.substr(url_start, url_end - url_start);

                // Find title
                size_t title_start = resp.body.find(">", url_end);
                if (title_start != std::string::npos) {
                    title_start++;
                    size_t title_end = resp.body.find("<", title_start);
                    if (title_end != std::string::npos) {
                        std::string title = resp.body.substr(title_start, title_end - title_start);
                        result << (result_count + 1) << ". " << title << "\n   " << url << "\n\n";
                        result_count++;
                    }
                }
            }
        }
        pos++;
    }

    if (result_count == 0) {
        return "No search results found for: " + query;
    }

    result << "[" << result_count << " results returned]";
    return result.str();
}

std::string ToolRegistry::TokenCountTool(const nlohmann::json& params) {
    std::string text = params.value("text", "");
    std::string model = params.value("model", std::string("claude-3"));

    if (text.empty()) {
        throw std::runtime_error("Text is required");
    }

    int tokens = TokenCounter::CountTokens(text, model);
    int estimated = TokenCounter::EstimateTokens(text);

    std::ostringstream result;
    result << "Token counting for model: " << model << "\n\n";
    result << "Text length: " << text.length() << " characters\n";
    result << "Estimated tokens (char/4): " << estimated << "\n";
    result << "Counted tokens: " << tokens << "\n";

    return result.str();
}

std::string ToolRegistry::TokenUsageTool(const nlohmann::json& params) {
    std::string model = params.value("model", std::string(""));

    auto& tracker = TokenTracker::GetInstance();

    std::ostringstream result;
    result << "=== Token Usage Statistics ===\n\n";

    if (model.empty()) {
        // Show total stats
        TokenStats total = tracker.GetTotalStats();
        result << "Total Usage:\n";
        result << "  Prompt tokens: " << total.prompt_tokens << "\n";
        result << "  Completion tokens: " << total.completion_tokens << "\n";
        result << "  Total tokens: " << total.total_tokens << "\n";
        result << "  Estimated cost: $" << std::fixed << std::setprecision(4) << total.cost_usd << "\n\n";

        // Show per-model stats
        auto all_stats = tracker.GetAllStats();
        if (!all_stats.empty()) {
            result << "Per-Model Usage:\n";
            for (const auto& [m, stats] : all_stats) {
                result << "  " << m << ":\n";
                result << "    Prompt: " << stats.prompt_tokens << ", Completion: " << stats.completion_tokens;
                result << ", Total: " << stats.total_tokens;
                result << ", Cost: $" << std::fixed << std::setprecision(4) << stats.cost_usd << "\n";
            }
        }
    } else {
        // Show stats for specific model
        TokenStats stats = tracker.GetModelStats(model);
        if (stats.total_tokens == 0) {
            return "No token usage recorded for model: " + model;
        }
        result << "Model: " << model << "\n";
        result << "  Prompt tokens: " << stats.prompt_tokens << "\n";
        result << "  Completion tokens: " << stats.completion_tokens << "\n";
        result << "  Total tokens: " << stats.total_tokens << "\n";
        result << "  Estimated cost: $" << std::fixed << std::setprecision(4) << stats.cost_usd << "\n";
    }

    return result.str();
}

std::string ToolRegistry::McpListToolsTool(const nlohmann::json& params) {
    std::string server = params.value("server", std::string(""));

    auto& mcp_client = McpClient::GetInstance();
    std::ostringstream result;

    std::vector<McpTool> tools;
    if (server.empty()) {
        tools = mcp_client.GetAvailableTools();
    } else {
        tools = mcp_client.GetServerTools(server);
    }

    if (tools.empty()) {
        return "No MCP tools available";
    }

    result << "=== Available MCP Tools ===\n\n";
    for (const auto& tool : tools) {
        result << "  " << tool.name << " (server: " << tool.server_name << ")\n";
        if (!tool.description.empty()) {
            result << "    " << tool.description << "\n";
        }
        if (!tool.input_schema.is_null()) {
            result << "    Input schema: " << tool.input_schema.dump(2) << "\n";
        }
        result << "\n";
    }

    result << "[" << tools.size() << " tool(s) available]";
    return result.str();
}

std::string ToolRegistry::McpCallToolTool(const nlohmann::json& params) {
    std::string tool_name = params.value("tool_name", "");
    nlohmann::json arguments = params.value("arguments", nlohmann::json::object());

    if (tool_name.empty()) {
        throw std::runtime_error("tool_name is required");
    }

    auto& mcp_client = McpClient::GetInstance();
    return mcp_client.CallTool(tool_name, arguments);
}

std::string ToolRegistry::GetMcpToolName(const McpTool& tool) const {
    return "mcp__" + tool.server_name + "__" + tool.name;
}

void ToolRegistry::RegisterMcpTools() {
    auto& mcp_client = McpClient::GetInstance();

    // Load configured servers and connect
    mcp_client.LoadServersFromFile();

    auto tools = mcp_client.GetAvailableTools();

    int registered_count = 0;
    for (const auto& mcp_tool : tools) {
        std::string registered_name = GetMcpToolName(mcp_tool);

        // Check if tool with same name already exists
        bool exists = false;
        for (const auto& schema : tool_schemas_) {
            if (schema.name == registered_name) {
                exists = true;
                break;
            }
        }

        if (exists) {
            continue;
        }

        // Register the MCP tool - capture tool details by value for later use
        std::string tool_name = mcp_tool.name;
        RegisterTool(
            registered_name,
            mcp_tool.description.empty() ? "MCP tool: " + mcp_tool.name : mcp_tool.description,
            mcp_tool.input_schema.is_null() ? nlohmann::json::object() : mcp_tool.input_schema,
            [tool_name](const nlohmann::json& p) {
                return McpClient::GetInstance().CallTool(tool_name, p);
            }
        );

        registered_count++;
        LOG_INFO("Registered MCP tool: {} -> {}", registered_name, mcp_tool.name);
    }

    LOG_INFO("Registered {} MCP tools", registered_count);
}

std::vector<std::string> ToolRegistry::GetRegisteredMcpServers() const {
    std::vector<std::string> servers;
    for (const auto& schema : tool_schemas_) {
        if (schema.name.find("mcp__") == 0) {
            // Extract server name: mcp__<server>__<tool>
            size_t first_sep = schema.name.find("__", 5);
            if (first_sep != std::string::npos) {
                std::string server = schema.name.substr(5, first_sep - 5);
                if (std::find(servers.begin(), servers.end(), server) == servers.end()) {
                    servers.push_back(server);
                }
            }
        }
    }
    return servers;
}

bool ToolRegistry::UnregisterMcpServer(const std::string& server_name) {
    std::string prefix = "mcp__" + server_name + "__";
    std::vector<std::string> tools_to_remove;

    for (const auto& schema : tool_schemas_) {
        if (schema.name.find(prefix) == 0) {
            tools_to_remove.push_back(schema.name);
        }
    }

    for (const auto& tool_name : tools_to_remove) {
        tools_.erase(tool_name);
    }

    // Remove from schemas
    tool_schemas_.erase(
        std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
            [&prefix](const ToolsSchema& schema) {
                return schema.name.find(prefix) == 0;
            }),
        tool_schemas_.end()
    );

    LOG_INFO("Unregistered {} MCP tools from server: {}", tools_to_remove.size(), server_name);
    return !tools_to_remove.empty();
}

std::string ToolRegistry::McpListResourcesTool(const nlohmann::json& /* params */) {
    auto& mcp_client = McpClient::GetInstance();
    auto resources = mcp_client.GetAvailableResources();

    std::ostringstream result;
    if (resources.empty()) {
        return "No MCP resources available";
    }

    result << "=== Available MCP Resources ===\n\n";
    for (const auto& res : resources) {
        result << "  " << res.name << "\n";
        result << "    URI: " << res.uri << "\n";
        if (!res.description.empty()) {
            result << "    " << res.description << "\n";
        }
        result << "    MIME type: " << res.mime_type << "\n\n";
    }

    result << "[" << resources.size() << " resource(s) available]";
    return result.str();
}

std::string ToolRegistry::McpReadResourceTool(const nlohmann::json& params) {
    std::string uri = params.value("uri", "");

    if (uri.empty()) {
        throw std::runtime_error("uri is required");
    }

    auto& mcp_client = McpClient::GetInstance();
    return mcp_client.ReadResource(uri);
}

// ==================== Git Tools ====================

std::string ToolRegistry::GitStatusTool(const nlohmann::json& params) {
    std::string path = params.value("path", workspace_path_);
    if (path.empty()) {
        path = workspace_path_;
    }

    std::string cmd = "cd \"" + path + "\" && git status 2>&1";
    std::ostringstream result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "Error: Failed to execute git status";
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result << buffer;
    }
    int status = pclose(pipe);

    if (status != 0) {
        return "Error: Not a git repository or git not available";
    }

    return result.str();
}

std::string ToolRegistry::GitDiffTool(const nlohmann::json& params) {
    std::string path = params.value("path", workspace_path_);
    bool cached = params.value("cached", false);

    if (path.empty()) {
        path = workspace_path_;
    }

    std::string cmd = "cd \"" + path + "\" && git diff ";
    if (cached) {
        cmd += "--cached ";
    }
    cmd += "2>&1";

    std::ostringstream result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "Error: Failed to execute git diff";
    }

    char buffer[4096];
    bool has_diff = false;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result << buffer;
        has_diff = true;
    }
    int status = pclose(pipe);

    if (status != 0) {
        return "Error: Failed to get git diff";
    }

    if (!has_diff) {
        return cached ? "No staged changes." : "No working directory changes.";
    }

    return result.str();
}

std::string ToolRegistry::GitLogTool(const nlohmann::json& params) {
    std::string path = params.value("path", workspace_path_);
    int max_count = params.value("max_count", 10);

    if (max_count <= 0) max_count = 10;
    if (max_count > 50) max_count = 50;  // Limit output

    if (path.empty()) {
        path = workspace_path_;
    }

    std::string cmd = "cd \"" + path + "\" && git log --oneline -n " + std::to_string(max_count) + " 2>&1";

    std::ostringstream result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "Error: Failed to execute git log";
    }

    char buffer[256];
    int count = 0;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result << buffer;
        count++;
    }
    pclose(pipe);

    if (count == 0) {
        return "No commit history found.";
    }

    result << "\n[" << count << " commit(s) shown]";
    return result.str();
}

std::string ToolRegistry::GitCommitTool(const nlohmann::json& params) {
    std::string message = params.value("message", "");
    bool do_all = params.value("all", false);

    if (message.empty()) {
        throw std::runtime_error("Commit message is required");
    }

    // Escape message for shell
    std::string escaped_msg;
    for (char c : message) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') {
            escaped_msg += '\\';
        }
        escaped_msg += c;
    }

    std::string cmd = "git ";
    if (do_all) {
        cmd += "commit -a ";
    } else {
        cmd += "commit ";
    }
    cmd += "-m \"" + escaped_msg + "\" 2>&1";

    std::ostringstream result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "Error: Failed to execute git commit";
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result << buffer;
    }
    int status = pclose(pipe);

    if (status != 0) {
        std::string output = result.str();
        if (output.find("nothing to commit") != std::string::npos) {
            return "Nothing to commit, working tree clean.";
        }
        return "Error: " + output;
    }

    return result.str();
}

std::string ToolRegistry::GitAddTool(const nlohmann::json& params) {
    std::vector<std::string> files;
    if (params.contains("files") && params["files"].is_array()) {
        files = params["files"].get<std::vector<std::string>>();
    }

    std::string cmd = "git add ";
    if (files.empty()) {
        cmd += "-A";  // Stage all changes
    } else {
        for (const auto& f : files) {
            cmd += "\"" + f + "\" ";
        }
    }
    cmd += " 2>&1";

    std::ostringstream result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "Error: Failed to execute git add";
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result << buffer;
    }
    int status = pclose(pipe);

    if (status != 0) {
        return "Error: " + result.str();
    }

    if (files.empty()) {
        return "All changes staged for commit.";
    }
    return "Staged " + std::to_string(files.size()) + " file(s) for commit.";
}

std::string ToolRegistry::GitBranchTool(const nlohmann::json& params) {
    std::string path = params.value("path", workspace_path_);
    std::string create_branch = params.value("create", "");
    bool checkout = params.value("checkout", false);

    if (path.empty()) {
        path = workspace_path_;
    }

    std::ostringstream result;

    // Create new branch if requested
    if (!create_branch.empty()) {
        std::string cmd = "cd \"" + path + "\" && git ";
        if (checkout) {
            cmd += "checkout -b \"" + create_branch + "\"";
        } else {
            cmd += "branch \"" + create_branch + "\"";
        }
        cmd += " 2>&1";

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            return "Error: Failed to create branch";
        }
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result << buffer;
        }
        pclose(pipe);

        result << "\n";
    }

    // List branches
    std::string cmd = "cd \"" + path + "\" && git branch 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "Error: Failed to list branches";
    }

    char buffer[256];
    int count = 0;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result << buffer;
        count++;
    }
    pclose(pipe);

    if (count == 0) {
        return "No branches found or not a git repository.";
    }

    result << "\n[" << count << " branch(es)]";
    return result.str();
}

std::string ToolRegistry::AskUserQuestionTool(const nlohmann::json& params) {
    auto& question_tool = aicode::AskUserQuestionTool::GetInstance();

    // Parse questions from parameters
    std::vector<Question> questions;
    if (params.contains("questions") && params["questions"].is_array()) {
        questions = question_tool.ParseQuestions(params["questions"]);
    }

    if (questions.empty()) {
        return "Error: No valid questions provided";
    }

    // Validate questions
    std::string error;
    if (!question_tool.ValidateQuestions(questions, error)) {
        return "Error: " + error;
    }

    // Ask questions and collect answers
    auto result = question_tool.Ask(questions);

    // Format result
    nlohmann::json json;
    json["questions"] = nlohmann::json::array();
    for (const auto& q : result.questions) {
        nlohmann::json qj;
        qj["question"] = q.question;
        qj["header"] = q.header;
        qj["options"] = nlohmann::json::array();
        for (const auto& opt : q.options) {
            nlohmann::json oj;
            oj["label"] = opt.label;
            oj["description"] = opt.description;
            oj["preview"] = opt.preview;
            qj["options"].push_back(oj);
        }
        qj["multiSelect"] = q.multiSelect;
        json["questions"].push_back(qj);
    }

    json["answers"] = nlohmann::json::object();
    for (const auto& [q, a] : result.answers) {
        json["answers"][q] = a;
    }

    return "User answered:\n" + json.dump(2);
}

std::string ToolRegistry::TodoWriteTool(const nlohmann::json& params) {
    auto& todo_tool = aicode::TodoWriteTool::GetInstance();

    std::string operation = params.value("operation", "list");

    if (operation == "list") {
        return todo_tool.FormatTodos();
    }

    if (operation == "create") {
        std::string content = params.value("content", "");
        if (content.empty()) {
            return "Error: content is required for create operation";
        }
        std::string priority = params.value("priority", "medium");
        std::string id = todo_tool.CreateTodo(content, priority);
        return "Created todo: " + id + "\n  Content: " + content + "\n  Priority: " + priority;
    }

    if (operation == "update") {
        std::string id = params.value("id", "");
        if (id.empty()) {
            return "Error: id is required for update operation";
        }
        std::string status = params.value("status", "pending");
        TodoStatus todo_status;
        if (status == "completed") {
            todo_status = TodoStatus::Completed;
        } else if (status == "cancelled") {
            todo_status = TodoStatus::Cancelled;
        } else {
            todo_status = TodoStatus::Pending;
        }
        if (todo_tool.UpdateTodoStatus(id, todo_status)) {
            return "Updated todo " + id + " status to " + status;
        }
        return "Error: Todo not found: " + id;
    }

    if (operation == "delete") {
        std::string id = params.value("id", "");
        if (id.empty()) {
            return "Error: id is required for delete operation";
        }
        if (todo_tool.DeleteTodo(id)) {
            return "Deleted todo: " + id;
        }
        return "Error: Todo not found: " + id;
    }

    if (operation == "clear") {
        int count = todo_tool.ClearCompleted();
        return "Cleared " + std::to_string(count) + " completed/cancelled todos";
    }

    return "Error: Unknown operation: " + operation + ". Use: list, create, update, delete, clear";
}

// LSP Tool implementations
std::string ToolRegistry::LspDiagnosticsTool(const nlohmann::json& params) {
    return aicode::LspTool::GetInstance().Diagnostics(params);
}

std::string ToolRegistry::LspGoToDefinitionTool(const nlohmann::json& params) {
    return aicode::LspTool::GetInstance().GoToDefinition(params);
}

std::string ToolRegistry::LspFindReferencesTool(const nlohmann::json& params) {
    return aicode::LspTool::GetInstance().FindReferences(params);
}

std::string ToolRegistry::LspGetHoverTool(const nlohmann::json& params) {
    return aicode::LspTool::GetInstance().GetHover(params);
}

std::string ToolRegistry::LspDocumentSymbolsTool(const nlohmann::json& params) {
    return aicode::LspTool::GetInstance().GetDocumentSymbols(params);
}

std::string ToolRegistry::LspWorkspaceSymbolsTool(const nlohmann::json& params) {
    return aicode::LspTool::GetInstance().WorkspaceSymbols(params);
}

std::string ToolRegistry::LspFormatDocumentTool(const nlohmann::json& params) {
    return aicode::LspTool::GetInstance().FormatDocument(params);
}

std::string ToolRegistry::LspAllDiagnosticsTool(const nlohmann::json& params) {
    return aicode::LspTool::GetInstance().GetAllDiagnostics(params);
}

std::string ToolRegistry::LspListServersTool(const nlohmann::json& params) {
    return aicode::LspTool::GetInstance().ListServers(params);
}

}  // namespace aicode
