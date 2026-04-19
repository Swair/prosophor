// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <io.h>
#include <codecvt>
#include <locale>
#endif

#include "tools/tool_registry.h"

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>

#ifndef _WIN32
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#include <unistd.h>
#endif

#include "common/log_wrapper.h"
#include "common/time_wrapper.h"
#include "common/curl_client.h"
#include "managers/token_tracker.h"
#include "managers/permission_manager.h"
#include "mcp/mcp_client.h"
#include "tools/agent_tools/agent_tool.h"
#include "tools/command_tools/background_run_tool.h"
#include "tools/search_tools/glob_tool.h"
#include "tools/search_tools/grep_tool.h"
#include "tools/task_tools/cron_tool.h"
#include "tools/task_tools/task_tool.h"
#include "tools/lsp_tools/lsp_tool.h"
#include "tools/worktree_tools/worktree_tool.h"

namespace aicode {

#ifdef _WIN32
/// Convert system locale output to UTF-8 on Windows
/// Skip conversion for MSYS2/Git Bash (already UTF-8)
static std::string ConvertToUTF8(const std::string& mbcs_str) {
    // Git Bash/MSYS2 already outputs UTF-8
    if (std::getenv("MSYSTEM")) {
        return mbcs_str;
    }

    int wide_len = MultiByteToWideChar(CP_ACP, 0, mbcs_str.c_str(), -1, nullptr, 0);
    if (wide_len <= 1) return mbcs_str;

    std::wstring wide(wide_len - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, mbcs_str.c_str(), -1, &wide[0], wide_len);

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 1) return mbcs_str;

    std::string result(utf8_len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &result[0], utf8_len, nullptr, nullptr);
    return result;
}
#endif

/// Format command result with exit code (DRY helper)
/// @param result Command output
/// @param status Exit code
/// @param error_prefix Optional error prefix when status != 0
/// @return Formatted result string
static std::string FormatCommandResult(const std::string& result, int status,
                                        const std::string& error_prefix = "") {
    if (status != 0 && !error_prefix.empty()) {
        return error_prefix + "[Exit code: " + std::to_string(status) + "]";
    }
    return result + "[Exit code: " + std::to_string(status) + "]";
}

/// Execute a shell command and capture output (cross-platform, thread-safe)
/// Inspired by QuantClaw::platform::exec_capture
/// @param command Command to execute
/// @param timeout_seconds Timeout in seconds (0 = no timeout)
/// @param workdir Working directory (empty = current)
/// @return pair<output, exit_code>
static std::pair<std::string, int> ExecuteCommand(const std::string& command,
                                                   int timeout_seconds = 0,
                                                   const std::string& workdir = "") {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr, write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        return {"Failed to create pipe", -1};
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    // Create input pipe for stdin (close write end to signal EOF immediately)
    HANDLE input_read = nullptr, input_write = nullptr;
    if (!CreatePipe(&input_read, &input_write, &sa, 0)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return {"Failed to create input pipe", -1};
    }
    SetHandleInformation(input_read, HANDLE_FLAG_INHERIT, 0);
    CloseHandle(input_write);  // Close write end - child gets EOF immediately

    // Directly execute command without shell (cmd /c or sh -c)
    // This lets Windows find the executable from PATH
    std::string cmd_line = command;
    std::vector<char> cmd_buf(cmd_line.begin(), cmd_line.end());
    cmd_buf.push_back('\0');

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    si.hStdInput = input_read;  // Use input pipe (will get EOF)
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};

    // lpCurrentDirectory needs a mutable string
    std::string mutable_workdir = workdir;

    BOOL ok = CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr,
                             TRUE, CREATE_NO_WINDOW,
                             workdir.empty() ? nullptr : const_cast<char*>(mutable_workdir.c_str()),
                             nullptr, &si, &pi);

    CloseHandle(write_pipe);

    if (!ok) {
        CloseHandle(input_read);
        CloseHandle(read_pipe);
        return {"Failed to create process", -1};
    }

    std::string result;
    char buffer[1024];
    auto start = aicode::Now();

    for (;;) {
        DWORD wait_ms = 100;
        if (timeout_seconds > 0) {
            int64_t elapsed_ms = aicode::ElapsedMillis(start);
            int64_t timeout_ms = timeout_seconds * 1000;
            if (elapsed_ms >= timeout_ms) {
                TerminateProcess(pi.hProcess, 1);
                CloseHandle(input_read);
                CloseHandle(read_pipe);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                return {"Command timeout", -2};
            }
            wait_ms = std::min(wait_ms, static_cast<DWORD>(timeout_ms - elapsed_ms));
        }

        DWORD avail = 0;
        if (!PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &avail, nullptr)) {
            if (WaitForSingleObject(pi.hProcess, wait_ms) == WAIT_OBJECT_0) break;
            continue;
        }
        if (avail == 0) {
            if (WaitForSingleObject(pi.hProcess, wait_ms) == WAIT_OBJECT_0) break;
            continue;
        }

        DWORD bytes_read = 0;
        if (ReadFile(read_pipe, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) && bytes_read > 0) {
            buffer[bytes_read] = '\0';
            result += ConvertToUTF8(std::string(buffer, bytes_read));
        }
    }

    // Drain remaining
    DWORD bytes_read = 0;
    while (ReadFile(read_pipe, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        result += ConvertToUTF8(std::string(buffer, bytes_read));
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(input_read);
    CloseHandle(read_pipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return {result, static_cast<int>(exit_code)};

#else
    // POSIX implementation using pipe + fork (inspired by QuantClaw::platform::exec_capture)
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return {"Failed to create pipe", -1};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return {"Failed to fork", -1};
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]);  // Close read end
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        // Change working directory if specified
        if (!workdir.empty()) {
            if (chdir(workdir.c_str()) != 0) {
                _exit(127);
            }
        }

        // Execute command
        execl("/bin/sh", "/bin/sh", "-c", command.c_str(), (char*)nullptr);
        _exit(127);  // If exec fails
    }

    // Parent process
    close(pipefd[1]);  // Close write end

    std::string result;
    char buffer[4096];
    auto start = aicode::Now();

    // Set non-blocking read with timeout
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 100ms

    for (;;) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(pipefd[0], &readfds);

        int select_result = select(pipefd[0] + 1, &readfds, nullptr, nullptr, &tv);

        if (select_result > 0 && FD_ISSET(pipefd[0], &readfds)) {
            ssize_t bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                result += std::string(buffer, bytes_read);
            } else if (bytes_read == 0) {
                break;  // EOF
            }
        }

        // Check timeout
        if (timeout_seconds > 0) {
            int64_t elapsed_ms = aicode::ElapsedMillis(start);
            if (elapsed_ms >= timeout_seconds * 1000) {
                kill(pid, SIGKILL);
                close(pipefd[0]);
                waitpid(pid, nullptr, 0);
                return {"Command timeout", -2};
            }
        }

        // Check if child exited
        int stat;
        int wait_result = waitpid(pid, &stat, WNOHANG);
        if (wait_result > 0) {
            // Child exited, drain remaining output
            while (read(pipefd[0], buffer, sizeof(buffer) - 1) > 0) {}
            close(pipefd[0]);
            return {result, WIFEXITED(stat) ? WEXITSTATUS(stat) : -1};
        } else if (wait_result < 0) {
            // Error
            close(pipefd[0]);
            return {result, -1};
        }
    }

    close(pipefd[0]);

    // Wait for child to complete
    int stat;
    waitpid(pid, &stat, 0);

    return {result, WIFEXITED(stat) ? WEXITSTATUS(stat) : -1};
#endif
}

ToolRegistry& ToolRegistry::GetInstance() {
    static ToolRegistry instance;
    return instance;
}

ToolRegistry::ToolRegistry()
    : workspace_path_("~/.aicode/workspace") {
    LOG_INFO("ToolRegistry initialized");

    // Initialize permission manager with default config
    auto& perm_manager = PermissionManager::GetInstance();
    perm_manager.Initialize();

    // Register built-in tools - ensures GetInstance() singleton has all tools
    RegisterBuiltinTools();
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
        params["query"]["description"] = "Search query";
        params["count"] = nlohmann::json::object();
        params["count"]["type"] = "integer";
        params["count"]["description"] = "Number of results (1-10, default 5)";
        RegisterTool("web_search", "Search the web using Brave, Tavily, or DuckDuckGo", params,
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

    // // mcp_list_tools - List available MCP tools
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["server"] = nlohmann::json::object();
    //     params["server"]["type"] = "string";
    //     params["server"]["description"] = "Server name (optional, omit for all)";
    //     RegisterTool("mcp_list_tools", "List available MCP tools", params,
    //                  [this](const nlohmann::json& p) { return McpListToolsTool(p); });
    // }

    // // mcp_call_tool - Call an MCP tool
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["tool_name"] = nlohmann::json::object();
    //     params["tool_name"]["type"] = "string";
    //     params["tool_name"]["description"] = "Name of the tool to call";
    //     params["arguments"] = nlohmann::json::object();
    //     params["arguments"]["type"] = "object";
    //     params["arguments"]["description"] = "Arguments to pass to the tool";
    //     RegisterTool("mcp_call_tool", "Call an MCP tool", params,
    //                  [this](const nlohmann::json& p) { return McpCallToolTool(p); });
    // }

    // // mcp_list_resources - List available MCP resources
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     RegisterTool("mcp_list_resources", "List available MCP resources", params,
    //                  [this](const nlohmann::json& p) { return McpListResourcesTool(p); });
    // }

    // // mcp_read_resource - Read an MCP resource
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["uri"] = nlohmann::json::object();
    //     params["uri"]["type"] = "string";
    //     params["uri"]["description"] = "URI of the resource to read";
    //     RegisterTool("mcp_read_resource", "Read an MCP resource", params,
    //                  [this](const nlohmann::json& p) { return McpReadResourceTool(p); });
    // }

    // // git_status - Show git repository status
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["path"] = nlohmann::json::object();
    //     params["path"]["type"] = "string";
    //     params["path"]["description"] = "Repository path (default: workspace)";
    //     RegisterTool("git_status", "Show git repository status", params,
    //                  [this](const nlohmann::json& p) { return GitStatusTool(p); });
    // }

    // // git_diff - Show changes in git repository
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["path"] = nlohmann::json::object();
    //     params["path"]["type"] = "string";
    //     params["path"]["description"] = "Repository path (default: workspace)";
    //     params["cached"] = nlohmann::json::object();
    //     params["cached"]["type"] = "boolean";
    //     params["cached"]["description"] = "Show staged changes (default: false)";
    //     RegisterTool("git_diff", "Show git changes", params,
    //                  [this](const nlohmann::json& p) { return GitDiffTool(p); });
    // }

    // // git_log - Show git commit history
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["path"] = nlohmann::json::object();
    //     params["path"]["type"] = "string";
    //     params["path"]["description"] = "Repository path (default: workspace)";
    //     params["max_count"] = nlohmann::json::object();
    //     params["max_count"]["type"] = "integer";
    //     params["max_count"]["description"] = "Maximum number of commits to show (default: 10)";
    //     RegisterTool("git_log", "Show git commit history", params,
    //                  [this](const nlohmann::json& p) { return GitLogTool(p); });
    // }

    // // git_commit - Create a git commit
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["message"] = nlohmann::json::object();
    //     params["message"]["type"] = "string";
    //     params["message"]["description"] = "Commit message";
    //     params["all"] = nlohmann::json::object();
    //     params["all"]["type"] = "boolean";
    //     params["all"]["description"] = "Auto-stage all changes before commit (default: false)";
    //     RegisterTool("git_commit", "Create a git commit", params,
    //                  [this](const nlohmann::json& p) { return GitCommitTool(p); });
    // }

    // // git_add - Stage file changes
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["files"] = nlohmann::json::object();
    //     params["files"]["type"] = "array";
    //     params["files"]["items"] = {{"type", "string"}};
    //     params["files"]["description"] = "Files to stage (empty = stage all)";
    //     RegisterTool("git_add", "Stage file changes for commit", params,
    //                  [this](const nlohmann::json& p) { return GitAddTool(p); });
    // }

    // // git_branch - List or create git branches
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["path"] = nlohmann::json::object();
    //     params["path"]["type"] = "string";
    //     params["path"]["description"] = "Repository path (default: workspace)";
    //     params["create"] = nlohmann::json::object();
    //     params["create"]["type"] = "string";
    //     params["create"]["description"] = "Branch name to create";
    //     params["checkout"] = nlohmann::json::object();
    //     params["checkout"]["type"] = "boolean";
    //     params["checkout"]["description"] = "Checkout the branch after creating (default: false)";
    //     RegisterTool("git_branch", "List or create git branches", params,
    //                  [this](const nlohmann::json& p) { return GitBranchTool(p); });
    // }

    // // ask_user_question - Interactive user questions
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["questions"] = nlohmann::json::object();
    //     params["questions"]["type"] = "array";
    //     params["questions"]["description"] = "List of questions to ask (1-4)";
    //     params["questions"]["items"] = nlohmann::json::object();
    //     params["questions"]["items"]["type"] = "object";
    //     params["questions"]["items"]["properties"] = nlohmann::json::object();
    //     params["questions"]["items"]["properties"]["question"] = nlohmann::json::object();
    //     params["questions"]["items"]["properties"]["question"]["type"] = "string";
    //     params["questions"]["items"]["properties"]["header"] = nlohmann::json::object();
    //     params["questions"]["items"]["properties"]["header"]["type"] = "string";
    //     params["questions"]["items"]["properties"]["options"] = nlohmann::json::object();
    //     params["questions"]["items"]["properties"]["options"]["type"] = "array";
    //     params["questions"]["items"]["properties"]["multiSelect"] = nlohmann::json::object();
    //     params["questions"]["items"]["properties"]["multiSelect"]["type"] = "boolean";
    //     RegisterTool("ask_user_question", "Ask the user interactive questions with multiple choice options", params,
    //                  [this](const nlohmann::json& p) { return this->AskUserQuestionTool(p); });
    // }

    // // todo_write - TODO list management
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["operation"] = nlohmann::json::object();
    //     params["operation"]["type"] = "string";
    //     params["operation"]["description"] = "Operation: create, update, delete, list, clear";
    //     params["id"] = nlohmann::json::object();
    //     params["id"]["type"] = "string";
    //     params["id"]["description"] = "Todo item ID (for update/delete)";
    //     params["content"] = nlohmann::json::object();
    //     params["content"]["type"] = "string";
    //     params["content"]["description"] = "Todo content (for create)";
    //     params["status"] = nlohmann::json::object();
    //     params["status"]["type"] = "string";
    //     params["status"]["description"] = "Status: pending, completed, cancelled (for update)";
    //     params["priority"] = nlohmann::json::object();
    //     params["priority"]["type"] = "string";
    //     params["priority"]["description"] = "Priority: low, medium, high (for create)";
    //     RegisterTool("todo_write", "Manage TODO list items", params,
    //                  [this](const nlohmann::json& p) { return this->TodoWriteTool(p); });
    // }

    // // Task management tools
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["subject"] = nlohmann::json::object();
    //     params["subject"]["type"] = "string";
    //     params["subject"]["description"] = "Task subject";
    //     params["description"] = nlohmann::json::object();
    //     params["description"]["type"] = "string";
    //     params["description"]["description"] = "Task description";
    //     params["active_form"] = nlohmann::json::object();
    //     params["active_form"]["type"] = "string";
    //     params["active_form"]["description"] = "Present continuous form for spinner display";
    //     RegisterTool("task_create", "Create a new task with subject and description", params,
    //                  [](const nlohmann::json& p) { return TaskTool::Execute("create", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["task_id"] = nlohmann::json::object();
    //     params["task_id"]["type"] = "string";
    //     params["task_id"]["description"] = "Task ID";
    //     RegisterTool("task_get", "Get task details by ID", params,
    //                  [](const nlohmann::json& p) { return TaskTool::Execute("get", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["task_id"] = nlohmann::json::object();
    //     params["task_id"]["type"] = "string";
    //     params["task_id"]["description"] = "Task ID";
    //     params["status"] = nlohmann::json::object();
    //     params["status"]["type"] = "string";
    //     params["status"]["description"] = "New status: pending, in_progress, completed, cancelled, failed";
    //     params["owner"] = nlohmann::json::object();
    //     params["owner"]["type"] = "string";
    //     params["owner"]["description"] = "Task owner/assignee";
    //     RegisterTool("task_update", "Update task status, owner, or description", params,
    //                  [](const nlohmann::json& p) { return TaskTool::Execute("update", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["status"] = nlohmann::json::object();
    //     params["status"]["type"] = "string";
    //     params["status"]["description"] = "Filter by status (optional)";
    //     RegisterTool("task_list", "List all tasks, optionally filtered by status", params,
    //                  [](const nlohmann::json& p) { return TaskTool::Execute("list", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["task_id"] = nlohmann::json::object();
    //     params["task_id"]["type"] = "string";
    //     params["task_id"]["description"] = "Task ID to delete";
    //     RegisterTool("task_delete", "Delete a task by ID", params,
    //                  [](const nlohmann::json& p) { return TaskTool::Execute("delete", p); });
    // }

    // // Cron scheduled task tools
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["cron"] = nlohmann::json::object();
    //     params["cron"]["type"] = "string";
    //     params["cron"]["description"] = "Cron expression (5-field: M H DoM Mon DoW)";
    //     params["prompt"] = nlohmann::json::object();
    //     params["prompt"]["type"] = "string";
    //     params["prompt"]["description"] = "Prompt to execute";
    //     params["recurring"] = nlohmann::json::object();
    //     params["recurring"]["type"] = "boolean";
    //     params["recurring"]["description"] = "Whether to repeat (default true)";
    //     params["durable"] = nlohmann::json::object();
    //     params["durable"]["type"] = "boolean";
    //     params["durable"]["description"] = "Persist to file (default false)";
    //     RegisterTool("cron_create", "Schedule a new recurring or one-shot task", params,
    //                  [](const nlohmann::json& p) { return CronTool::Execute("create", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     RegisterTool("cron_list", "List all scheduled tasks", params,
    //                  [](const nlohmann::json& p) { return CronTool::Execute("list", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["task_id"] = nlohmann::json::object();
    //     params["task_id"]["type"] = "string";
    //     params["task_id"]["description"] = "Task ID to delete";
    //     RegisterTool("cron_delete", "Delete a scheduled task", params,
    //                  [](const nlohmann::json& p) { return CronTool::Execute("delete", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["task_id"] = nlohmann::json::object();
    //     params["task_id"]["type"] = "string";
    //     params["task_id"]["description"] = "Task ID to run immediately";
    //     RegisterTool("cron_run", "Run a scheduled task immediately", params,
    //                  [](const nlohmann::json& p) { return CronTool::Execute("run", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["task_id"] = nlohmann::json::object();
    //     params["task_id"]["type"] = "string";
    //     params["task_id"]["description"] = "Task ID to pause";
    //     RegisterTool("cron_pause", "Pause a scheduled task", params,
    //                  [](const nlohmann::json& p) { return CronTool::Execute("pause", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["task_id"] = nlohmann::json::object();
    //     params["task_id"]["type"] = "string";
    //     params["task_id"]["description"] = "Task ID to resume";
    //     RegisterTool("cron_resume", "Resume a paused scheduled task", params,
    //                  [](const nlohmann::json& p) { return CronTool::Execute("resume", p); });
    // }

    // // LSP tools - Language Server Protocol integration
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["uri"] = nlohmann::json::object();
    //     params["uri"]["type"] = "string";
    //     params["uri"]["description"] = "File URI (file://path/to/file)";
    //     params["severity"] = nlohmann::json::object();
    //     params["severity"]["type"] = "string";
    //     params["severity"]["description"] = "Filter by severity: error|warning|information|hint";
    //     RegisterTool("lsp_diagnostics", "Get diagnostics (errors, warnings) for a file", params,
    //                  [this](const nlohmann::json& p) { return this->LspDiagnosticsTool(p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["uri"] = nlohmann::json::object();
    //     params["uri"]["type"] = "string";
    //     params["uri"]["description"] = "File URI (file://path/to/file)";
    //     params["line"] = nlohmann::json::object();
    //     params["line"]["type"] = "integer";
    //     params["line"]["description"] = "Line number (0-based)";
    //     params["character"] = nlohmann::json::object();
    //     params["character"]["type"] = "integer";
    //     params["character"]["description"] = "Character position (0-based)";
    //     RegisterTool("lsp_go_to_definition", "Go to definition of symbol at position", params,
    //                  [this](const nlohmann::json& p) { return this->LspGoToDefinitionTool(p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["uri"] = nlohmann::json::object();
    //     params["uri"]["type"] = "string";
    //     params["uri"]["description"] = "File URI";
    //     params["line"] = nlohmann::json::object();
    //     params["line"]["type"] = "integer";
    //     params["line"]["description"] = "Line number (0-based)";
    //     params["character"] = nlohmann::json::object();
    //     params["character"]["type"] = "integer";
    //     params["character"]["description"] = "Character position (0-based)";
    //     params["include_declaration"] = nlohmann::json::object();
    //     params["include_declaration"]["type"] = "boolean";
    //     params["include_declaration"]["description"] = "Include declaration in results";
    //     RegisterTool("lsp_find_references", "Find all references to symbol at position", params,
    //                  [this](const nlohmann::json& p) { return this->LspFindReferencesTool(p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["uri"] = nlohmann::json::object();
    //     params["uri"]["type"] = "string";
    //     params["uri"]["description"] = "File URI";
    //     params["line"] = nlohmann::json::object();
    //     params["line"]["type"] = "integer";
    //     params["line"]["description"] = "Line number (0-based)";
    //     params["character"] = nlohmann::json::object();
    //     params["character"]["type"] = "integer";
    //     params["character"]["description"] = "Character position (0-based)";
    //     RegisterTool("lsp_get_hover", "Get hover information (type, docs) for symbol at position", params,
    //                  [this](const nlohmann::json& p) { return this->LspGetHoverTool(p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["uri"] = nlohmann::json::object();
    //     params["uri"]["type"] = "string";
    //     params["uri"]["description"] = "File URI";
    //     RegisterTool("lsp_document_symbols", "Get all symbols (functions, classes, etc.) in document", params,
    //                  [this](const nlohmann::json& p) { return this->LspDocumentSymbolsTool(p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["query"] = nlohmann::json::object();
    //     params["query"]["type"] = "string";
    //     params["query"]["description"] = "Symbol search query";
    //     RegisterTool("lsp_workspace_symbols", "Search for symbols across entire workspace", params,
    //                  [this](const nlohmann::json& p) { return this->LspWorkspaceSymbolsTool(p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["uri"] = nlohmann::json::object();
    //     params["uri"]["type"] = "string";
    //     params["uri"]["description"] = "File URI";
    //     RegisterTool("lsp_format_document", "Format document according to language rules", params,
    //                  [this](const nlohmann::json& p) { return this->LspFormatDocumentTool(p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     RegisterTool("lsp_all_diagnostics", "Get all diagnostics across all open files", params,
    //                  [this](const nlohmann::json& p) { return this->LspAllDiagnosticsTool(p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     RegisterTool("lsp_list_servers", "List all registered LSP servers", params,
    //                  [this](const nlohmann::json& p) { return this->LspListServersTool(p); });
    // }

    // // Agent tools - Sub-agent launching and skill execution
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["prompt"] = nlohmann::json::object();
    //     params["prompt"]["type"] = "string";
    //     params["prompt"]["description"] = "Task description for the sub-agent";
    //     params["subagent_type"] = nlohmann::json::object();
    //     params["subagent_type"]["type"] = "string";
    //     params["subagent_type"]["description"] = "Type: general-purpose, Explore, Plan, Code (optional)";
    //     params["model"] = nlohmann::json::object();
    //     params["model"]["type"] = "string";
    //     params["model"]["description"] = "Model to use (optional)";
    //     RegisterTool("agent_launch", "Launch a sub-agent to handle a complex task", params,
    //                  [](const nlohmann::json& p) { return AgentTool::Execute("launch", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["skill"] = nlohmann::json::object();
    //     params["skill"]["type"] = "string";
    //     params["skill"]["description"] = "Name of the skill to execute";
    //     params["args"] = nlohmann::json::object();
    //     params["args"]["type"] = "string";
    //     params["args"]["description"] = "Arguments for the skill (optional)";
    //     RegisterTool("agent_skill", "Execute a skill", params,
    //                  [](const nlohmann::json& p) { return AgentTool::Execute("skill", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["query"] = nlohmann::json::object();
    //     params["query"]["type"] = "string";
    //     params["query"]["description"] = "Search query to find tools";
    //     RegisterTool("agent_tool_search", "Search for available tools by name or description", params,
    //                  [](const nlohmann::json& p) { return AgentTool::Execute("tool_search", p); });
    // }

    // // Worktree tools - git worktree management for task isolation
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["name"] = nlohmann::json::object();
    //     params["name"]["type"] = "string";
    //     params["name"]["description"] = "Worktree name";
    //     params["task_id"] = nlohmann::json::object();
    //     params["task_id"]["type"] = "string";
    //     params["task_id"]["description"] = "Associated task ID (optional)";
    //     params["base_branch"] = nlohmann::json::object();
    //     params["base_branch"]["type"] = "string";
    //     params["base_branch"]["description"] = "Base branch to create from (optional)";
    //     RegisterTool("worktree_create", "Create a new git worktree for task isolation", params,
    //                  [](const nlohmann::json& p) { return WorktreeTool::GetInstance().Execute("create", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["name"] = nlohmann::json::object();
    //     params["name"]["type"] = "string";
    //     params["name"]["description"] = "Worktree name";
    //     params["force"] = nlohmann::json::object();
    //     params["force"]["type"] = "boolean";
    //     params["force"]["description"] = "Force removal (default: false)";
    //     params["complete_task"] = nlohmann::json::object();
    //     params["complete_task"]["type"] = "boolean";
    //     params["complete_task"]["description"] = "Complete associated task (default: false)";
    //     RegisterTool("worktree_remove", "Remove a git worktree", params,
    //                  [](const nlohmann::json& p) { return WorktreeTool::GetInstance().Execute("remove", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["name"] = nlohmann::json::object();
    //     params["name"]["type"] = "string";
    //     params["name"]["description"] = "Worktree name";
    //     RegisterTool("worktree_keep", "Keep a worktree (mark as persistent)", params,
    //                  [](const nlohmann::json& p) { return WorktreeTool::GetInstance().Execute("keep", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["name"] = nlohmann::json::object();
    //     params["name"]["type"] = "string";
    //     params["name"]["description"] = "Worktree name";
    //     RegisterTool("worktree_get", "Get worktree details", params,
    //                  [](const nlohmann::json& p) { return WorktreeTool::GetInstance().Execute("get", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     RegisterTool("worktree_list", "List all worktrees", params,
    //                  [](const nlohmann::json& p) { return WorktreeTool::GetInstance().Execute("list", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["worktree"] = nlohmann::json::object();
    //     params["worktree"]["type"] = "string";
    //     params["worktree"]["description"] = "Worktree name";
    //     params["command"] = nlohmann::json::object();
    //     params["command"]["type"] = "string";
    //     params["command"]["description"] = "Command to execute";
    //     params["timeout"] = nlohmann::json::object();
    //     params["timeout"]["type"] = "integer";
    //     params["timeout"]["description"] = "Timeout in seconds (default: 300)";
    //     RegisterTool("worktree_exec", "Execute a command in a worktree", params,
    //                  [](const nlohmann::json& p) { return WorktreeTool::GetInstance().Execute("exec", p); });
    // }

    // // Background run tools - async command execution
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["command"] = nlohmann::json::object();
    //     params["command"]["type"] = "string";
    //     params["command"]["description"] = "Shell command to run in background";
    //     params["cwd"] = nlohmann::json::object();
    //     params["cwd"]["type"] = "string";
    //     params["cwd"]["description"] = "Working directory (optional)";
    //     RegisterTool("background_run", "Run a command in background", params,
    //                  [](const nlohmann::json& p) { return BackgroundRunTool::GetInstance().Execute("run", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["task_id"] = nlohmann::json::object();
    //     params["task_id"]["type"] = "string";
    //     params["task_id"]["description"] = "Background task ID";
    //     RegisterTool("background_get", "Get background task status and result", params,
    //                  [](const nlohmann::json& p) { return BackgroundRunTool::GetInstance().Execute("get", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     RegisterTool("background_list", "List all background tasks", params,
    //                  [](const nlohmann::json& p) { return BackgroundRunTool::GetInstance().Execute("list", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["task_id"] = nlohmann::json::object();
    //     params["task_id"]["type"] = "string";
    //     params["task_id"]["description"] = "Background task ID";
    //     RegisterTool("background_cancel", "Cancel a background task", params,
    //                  [](const nlohmann::json& p) { return BackgroundRunTool::GetInstance().Execute("cancel", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     RegisterTool("background_drain", "Drain background task notifications", params,
    //                  [](const nlohmann::json& p) { return BackgroundRunTool::GetInstance().Execute("drain", p); });
    // }

    // // Task claim and scan tools for autonomous agents
    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     params["task_id"] = nlohmann::json::object();
    //     params["task_id"]["type"] = "string";
    //     params["task_id"]["description"] = "Task ID to claim";
    //     params["agent_id"] = nlohmann::json::object();
    //     params["agent_id"]["type"] = "string";
    //     params["agent_id"]["description"] = "Agent ID claiming the task";
    //     RegisterTool("task_claim", "Claim an unclaimed task", params,
    //                  [](const nlohmann::json& p) { return TaskTool::Execute("claim", p); });
    // }

    // {
    //     nlohmann::json params = nlohmann::json::object();
    //     RegisterTool("task_scan_unclaimed", "Scan for unclaimed tasks", params,
    //                  [](const nlohmann::json& p) { return TaskTool::Execute("scan_unclaimed", p); });
    // }

    // // Register MCP tools dynamically
    // RegisterMcpTools();

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
        throw std::runtime_error("Executing tool: not found: " + tool_name);
    }

    // Check permissions before executing tool
    auto& perm_manager = PermissionManager::GetInstance();
    auto perm_result = perm_manager.CheckPermission(tool_name, parameters);

    if (perm_result.level == PermissionLevel::Deny) {
        LOG_WARN("Executing tool: {} denied: {}", tool_name, perm_result.reason);
        throw std::runtime_error("Tool execution denied: " + perm_result.reason);
    }

    LOG_DEBUG("Executing tool: {} - Permission check passed", tool_name);

    if (perm_result.level == PermissionLevel::Ask) {
        // Request user confirmation
        if (!perm_manager.RequestUserConfirmation(tool_name, parameters, perm_result.reason)) {
            LOG_WARN("Executing tool: {} confirmation denied by user", tool_name);
            throw std::runtime_error("Executing tool: execution denied by user");
        }
    }
    LOG_DEBUG("Executing tool: {} - Permission granted", tool_name);
    LOG_DEBUG("Executing tool: {}", tool_name);

    std::string ret = it->second(parameters);

    LOG_DEBUG("Executing tool: {} execution result: {}", tool_name, ret);
    return ret;
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
        throw std::runtime_error("BashTool: Command is required");
    }

    // Extract optional parameters
    int timeout = params.value("timeout", 30);  // Default 30s timeout
    std::string workdir = params.value("workdir", "");

    auto [result, status] = ExecuteCommand(command, timeout, workdir);

    // Handle timeout (-2) or fatal error (-1)
    if (status == -2) {
        throw std::runtime_error("BashTool: Command timeout after " + std::to_string(timeout) + "s: " + command);
    }
    if (status == -1) {
        throw std::runtime_error("BashTool: Failed to execute command: " + command);
    }

    // Always return output + exit code, even if non-zero
    // Let the LLM decide how to interpret the result
    return FormatCommandResult(result, status);
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

// URL decode helper
static std::string url_decode(const std::string& input) {
    std::ostringstream result;
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            int hi = 0;
            if (std::sscanf(input.substr(i + 1, 2).c_str(), "%x", &hi) == 1) {
                result << static_cast<char>(hi);
                i += 2;
            } else {
                result << input[i];
            }
        } else if (input[i] == '+') {
            result << ' ';
        } else {
            result << input[i];
        }
    }
    return result.str();
}

// Simple HTML to text conversion (no regex to avoid <regex> dependency)
static std::string html_to_text(const std::string& html) {
    std::string text = html;
    // Simple string replacements for HTML entities
    auto replace_all = [](std::string& s, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.length(), to);
            pos += to.length();
        }
    };
    replace_all(text, "&nbsp;", " ");
    replace_all(text, "&amp;", "&");
    replace_all(text, "&lt;", "<");
    replace_all(text, "&gt;", ">");
    replace_all(text, "&quot;", "\"");
    replace_all(text, "&#39;", "'");

    // Strip HTML tags
    std::string result;
    bool in_tag = false;
    for (char c : text) {
        if (c == '<') {
            in_tag = true;
        } else if (c == '>') {
            in_tag = false;
        } else if (!in_tag) {
            result += c;
        }
    }

    // Clean whitespace
    std::string cleaned;
    bool last_space = true;
    for (char c : result) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!last_space) {
                cleaned += ' ';
                last_space = true;
            }
        } else {
            cleaned += c;
            last_space = false;
        }
    }

    // Trim
    if (!cleaned.empty() && cleaned.front() == ' ') cleaned = cleaned.substr(1);
    if (!cleaned.empty() && cleaned.back() == ' ') cleaned.pop_back();

    return cleaned;
}

std::string ToolRegistry::WebSearchTool(const nlohmann::json& params) {
    std::string query = params.value("query", "");
    int count = params.value("count", 5);
    if (query.empty()) {
        throw std::runtime_error("Query is required");
    }
    if (count < 1) count = 5;
    if (count > 10) count = 10;

    // Get API keys from environment
    const char* brave_key = std::getenv("BRAVE_API_KEY");
    const char* tavily_key = std::getenv("TAVILY_API_KEY");
    const char* perp_key = std::getenv("PERPLEXITY_API_KEY");
    const char* xai_key = std::getenv("XAI_API_KEY");

    std::string last_error;
    std::ostringstream result;

    // --- Brave Search (API key required) ---
    if (brave_key && *brave_key) {
        try {
            char* escaped_raw = curl_easy_escape(nullptr, query.c_str(), (int)query.length());
            std::string escaped(escaped_raw);
            curl_free(escaped_raw);

            std::string url = "https://api.search.brave.com/res/v1/web/search?q=" + escaped + "&count=" + std::to_string(count);

            HttpRequest req;
            req.url = url;
            req.timeout_seconds = 15;
            req.user_agent = "AiCode/1.0";

            aicode::HeaderList headers;
            headers.append("Accept: application/json");
            headers.append("Accept-Encoding: identity");
            headers.append(("X-Subscription-Token: " + std::string(brave_key)).c_str());
            req.headers = headers.get();

            HttpResponse resp = HttpClient::Get(req);
            if (resp.failed()) {
                throw std::runtime_error("Brave Search: " + resp.error + " (HTTP " + std::to_string(resp.status_code) + ")");
            }

            auto j = nlohmann::json::parse(resp.body);
            result << "Search results (Brave): " << query << "\n\n";

            if (j.contains("web") && j["web"].contains("results")) {
                int i = 0;
                for (const auto& r : j["web"]["results"]) {
                    if (i >= count) break;
                    std::string title = r.value("title", "");
                    std::string url = r.value("url", "");
                    std::string desc = r.value("description", "");
                    result << (i + 1) << ". " << title << "\n   " << url << "\n   " << desc << "\n\n";
                    i++;
                }
                if (i > 0) return result.str();
            }
        } catch (const std::exception& e) {
            last_error = std::string("Brave: ") + e.what();
        }
    }

    // --- Tavily Search (API key required) ---
    if (tavily_key && *tavily_key) {
        try {
            nlohmann::json body = {
                {"api_key", tavily_key},
                {"query", query},
                {"max_results", count},
                {"search_depth", "basic"}
            };

            HttpRequest req;
            req.url = "https://api.tavily.com/search";
            req.post_data = body.dump();
            req.timeout_seconds = 15;

            aicode::HeaderList headers;
            headers.append("Content-Type: application/json");
            req.headers = headers.get();

            HttpResponse resp = HttpClient::Post(req);
            if (resp.failed()) {
                throw std::runtime_error("Tavily: " + resp.error + " (HTTP " + std::to_string(resp.status_code) + ")");
            }

            auto j = nlohmann::json::parse(resp.body);
            result.str("");
            result << "Search results (Tavily): " << query << "\n\n";

            if (j.contains("results")) {
                int i = 0;
                for (const auto& r : j["results"]) {
                    if (i >= count) break;
                    std::string title = r.value("title", "");
                    std::string url = r.value("url", "");
                    std::string desc = r.value("content", "");
                    result << (i + 1) << ". " << title << "\n   " << url << "\n   " << desc << "\n\n";
                    i++;
                }
                if (i > 0) return result.str();
            }
        } catch (const std::exception& e) {
            last_error = std::string("Tavily: ") + e.what();
        }
    }

    // --- DuckDuckGo HTML (no API key needed) ---
    try {
        char* encoded_raw = curl_easy_escape(nullptr, query.c_str(), (int)query.length());
        std::string encoded(encoded_raw);
        curl_free(encoded_raw);

        std::string url = "https://html.duckduckgo.com/html/?q=" + encoded;

        HttpRequest req;
        req.url = url;
        req.timeout_seconds = 15;
        req.user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";

        aicode::HeaderList headers;
        headers.append("Accept: text/html");
        req.headers = headers.get();

        HttpResponse resp = HttpClient::Get(req);
        if (resp.success() && !resp.body.empty()) {
            result.str("");
            result << "Search results (DuckDuckGo): " << query << "\n\n";

            // Parse DuckDuckGo HTML results
            // Use simple string matching instead of regex (avoid encoding issues)
            std::vector<std::pair<std::string, std::string>> links;
            std::vector<std::string> snippets;

            // Find result links: class="result__a"
            size_t pos = 0;
            while ((pos = resp.body.find("class=\"result__a\"", pos)) != std::string::npos && (int)links.size() < count) {
                // Find href
                size_t href_start = resp.body.find("href=\"", pos);
                if (href_start == std::string::npos || href_start > pos + 200) {
                    pos++;
                    continue;
                }
                href_start += 6;
                size_t href_end = resp.body.find("\"", href_start);
                if (href_end == std::string::npos) {
                    pos++;
                    continue;
                }
                std::string url = resp.body.substr(href_start, href_end - href_start);

                // Find title (between > and </a>)
                size_t title_start = resp.body.find(">", href_end);
                if (title_start == std::string::npos || title_start > href_end + 300) {
                    pos++;
                    continue;
                }
                size_t title_end = resp.body.find("</a>", title_start);
                if (title_end == std::string::npos) {
                    pos++;
                    continue;
                }
                std::string title = html_to_text(resp.body.substr(title_start, title_end - title_start));
                links.emplace_back(url, title);
                pos = title_end;
            }

            // Find snippets: class="result__snippet"
            pos = 0;
            while ((pos = resp.body.find("class=\"result__snippet\"", pos)) != std::string::npos && (int)snippets.size() < count) {
                size_t start = resp.body.find(">", pos);
                if (start == std::string::npos || start > pos + 200) {
                    pos++;
                    continue;
                }
                size_t end = resp.body.find("</a>", start);
                if (end == std::string::npos) {
                    pos++;
                    continue;
                }
                snippets.push_back(html_to_text(resp.body.substr(start, end - start)));
                pos = end;
            }

            int n = std::min(count, (int)links.size());
            for (int i = 0; i < n; ++i) {
                // URL decode for DuckDuckGo redirect URLs
                std::string url = links[i].first;
                if (url.find("uddg=") != std::string::npos) {
                    size_t pos = url.find("uddg=") + 5;
                    size_t amp = url.find('&', pos);
                    std::string encoded = (amp != std::string::npos) ? url.substr(pos, amp - pos) : url.substr(pos);
                    url = url_decode(encoded);
                }
                result << (i + 1) << ". " << links[i].second << "\n   " << url;
                if (i < (int)snippets.size()) {
                    result << "\n   " << snippets[i];
                }
                result << "\n\n";
            }

            if (n > 0) {
                result << "[" << n << " results from DuckDuckGo]";
                return result.str();
            }
        }
    } catch (const std::exception& e) {
        last_error = std::string("DuckDuckGo: ") + e.what();
    }

    // No provider succeeded
    std::string error_msg = "web_search: no provider succeeded.";
    if (!last_error.empty()) {
        error_msg += " Last error: " + last_error;
    }
    error_msg += " Configure BRAVE_API_KEY, TAVILY_API_KEY, or use DuckDuckGo (no key required).";
    return error_msg;
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

    std::string cmd = "cd \"" + path + "\" && git status";
    auto [result, status] = ExecuteCommand(cmd);

    if (status != 0) {
        return FormatCommandResult(result, status, "Error: Not a git repository or git not available");
    }
    return FormatCommandResult(result, status);
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

    auto [result, status] = ExecuteCommand(cmd);

    if (status != 0) {
        return FormatCommandResult(result, status, "Error: Failed to get git diff");
    }

    if (result.empty()) {
        return cached ? "No staged changes.[Exit code: 0]" : "No working directory changes.[Exit code: 0]";
    }

    return FormatCommandResult(result, status);
}

std::string ToolRegistry::GitLogTool(const nlohmann::json& params) {
    std::string path = params.value("path", workspace_path_);
    int max_count = params.value("max_count", 10);

    if (max_count <= 0) max_count = 10;
    if (max_count > 50) max_count = 50;

    if (path.empty()) {
        path = workspace_path_;
    }

    std::string cmd = "cd \"" + path + "\" && git log --oneline -n " + std::to_string(max_count);
    auto [result, status] = ExecuteCommand(cmd);

    if (status != 0) {
        return FormatCommandResult(result, status, "Error: Failed to execute git log");
    }

    int count = static_cast<int>(std::count(result.begin(), result.end(), '\n'));
    if (count == 0) {
        return "No commit history found.[Exit code: 0]";
    }

    return result + "\n[" + std::to_string(count) + " commit(s) shown][Exit code: " + std::to_string(status) + "]";
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
    cmd += "-m \"" + escaped_msg + "\"";

    auto [result, status] = ExecuteCommand(cmd);

    if (status != 0) {
        if (result.find("nothing to commit") != std::string::npos) {
            return "Nothing to commit, working tree clean.[Exit code: " + std::to_string(status) + "]";
        }
        return FormatCommandResult(result, status, "Error: ");
    }

    return FormatCommandResult(result, status);
}

std::string ToolRegistry::GitAddTool(const nlohmann::json& params) {
    std::vector<std::string> files;
    if (params.contains("files") && params["files"].is_array()) {
        files = params["files"].get<std::vector<std::string>>();
    }

    std::string cmd = "git add ";
    if (files.empty()) {
        cmd += "-A";
    } else {
        for (const auto& f : files) {
            cmd += "\"" + f + "\" ";
        }
    }

    auto [result, status] = ExecuteCommand(cmd);

    if (status != 0) {
        return FormatCommandResult(result, status, "Error: ");
    }

    if (files.empty()) {
        return "All changes staged for commit.[Exit code: " + std::to_string(status) + "]";
    }
    return "Staged " + std::to_string(files.size()) + " file(s) for commit.[Exit code: " + std::to_string(status) + "]";
}

std::string ToolRegistry::GitBranchTool(const nlohmann::json& params) {
    std::string path = params.value("path", workspace_path_);
    std::string create_branch = params.value("create", "");
    bool checkout = params.value("checkout", false);

    if (path.empty()) {
        path = workspace_path_;
    }

    std::string result;

    // Create new branch if requested
    if (!create_branch.empty()) {
        std::string cmd = "cd \"" + path + "\" && git ";
        if (checkout) {
            cmd += "checkout -b \"" + create_branch + "\"";
        } else {
            cmd += "branch \"" + create_branch + "\"";
        }

        auto [branch_result, status] = ExecuteCommand(cmd);
        result += branch_result + "\n";
    }

    // List branches
    std::string cmd = "cd \"" + path + "\" && git branch";
    auto [list_result, status] = ExecuteCommand(cmd);

    int count = static_cast<int>(std::count(list_result.begin(), list_result.end(), '\n'));
    result += list_result;

    if (count == 0) {
        return "No branches found or not a git repository.";
    }

    result += "\n[" + std::to_string(count) + " branch(es)]";
    return result;
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
