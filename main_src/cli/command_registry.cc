// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "cli/command_registry.h"

#include <sstream>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <set>

#include "common/log_wrapper.h"
#include "managers/token_tracker.h"
#include "agents/task_manager.h"
#include "managers/session_manager.h"
#include "managers/plugin_manager.h"
#include "agents/plan_mode.h"
#include "managers/memory_manager.h"
#include "core/compact_service.h"
#include "managers/permission_manager.h"
#include "common/config.h"
#include "common/effort_config.h"
#include "managers/worktree_manager.h"
#include "services/cron_scheduler.h"
#include "services/lsp_manager.h"
#include "mcp/mcp_client.h"

namespace aicode {

CommandRegistry& CommandRegistry::GetInstance() {
    static CommandRegistry instance;
    return instance;
}

void CommandRegistry::Initialize() {
    // /help
    {
        Command cmd;
        cmd.name = "help";
        cmd.description = "Show help information";
        cmd.usage = "/help [command]";
        cmd.aliases = {"h", "?"};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdHelp(ctx, args);
        };
        cmd.completer = [this](const std::string& partial) {
            return CompleteCommand(partial);
        };
        RegisterCommand(cmd);
    }

    // /cost
    {
        Command cmd;
        cmd.name = "cost";
        cmd.description = "Show token usage and cost statistics";
        cmd.usage = "/cost [model]";
        cmd.aliases = {"usage", "tokens"};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdCost(ctx, args);
        };
        RegisterCommand(cmd);
    }

    // /status
    {
        Command cmd;
        cmd.name = "status";
        cmd.description = "Show git repository status";
        cmd.usage = "/status [path]";
        cmd.aliases = {"git_status", "gs"};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdStatus(ctx, args);
        };
        RegisterCommand(cmd);
    }

    // /diff
    {
        Command cmd;
        cmd.name = "diff";
        cmd.description = "Show git diff";
        cmd.usage = "/diff [--cached] [path]";
        cmd.aliases = {"git_diff", "gd"};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdDiff(ctx, args);
        };
        RegisterCommand(cmd);
    }

    // /commit
    {
        Command cmd;
        cmd.name = "commit";
        cmd.description = "Create a git commit";
        cmd.usage = "/commit [-a] -m \"message\"";
        cmd.aliases = {"git_commit", "gc"};
        cmd.requires_args = true;
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdCommit(ctx, args);
        };
        RegisterCommand(cmd);
    }

    // /tasks
    {
        Command cmd;
        cmd.name = "tasks";
        cmd.description = "Manage tasks";
        cmd.usage = "/tasks [list|add|complete|delete]";
        cmd.aliases = {"task"};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdTasks(ctx, args);
        };
        RegisterCommand(cmd);
    }

    // /config
    {
        Command cmd;
        cmd.name = "config";
        cmd.description = "Show or edit configuration";
        cmd.usage = "/config [key] [value]";
        cmd.aliases = {"cfg", "settings"};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdConfig(ctx, args);
        };
        RegisterCommand(cmd);
    }

    // /clear
    {
        Command cmd;
        cmd.name = "clear";
        cmd.description = "Clear the screen";
        cmd.usage = "/clear";
        cmd.aliases = {"cls"};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdClear(ctx, args);
        };
        RegisterCommand(cmd);
    }

    // /mcp
    {
        Command cmd;
        cmd.name = "mcp";
        cmd.description = "Manage MCP servers";
        cmd.usage = "/mcp [list|add|remove|status]";
        cmd.aliases = {};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdMcp(ctx, args);
        };
        RegisterCommand(cmd);
    }

    // /resume
    {
        Command cmd;
        cmd.name = "resume";
        cmd.description = "Resume last session";
        cmd.usage = "/resume [session_id]";
        cmd.aliases = {"restore"};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdResume(ctx, args);
        };
        RegisterCommand(cmd);
    }

    // /sessions
    {
        Command cmd;
        cmd.name = "sessions";
        cmd.description = "List saved sessions";
        cmd.usage = "/sessions";
        cmd.aliases = {"session", "ls"};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdSessions(ctx, args);
        };
        RegisterCommand(cmd);
    }

    // /compact
    {
        Command cmd;
        cmd.name = "compact";
        cmd.description = "Compact conversation context";
        cmd.usage = "/compact";
        cmd.aliases = {};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdCompact(ctx, args);
        };
        RegisterCommand(cmd);
    }

    // /context - Context visualization
    {
        Command cmd;
        cmd.name = "context";
        cmd.description = "Show context usage and token statistics";
        cmd.usage = "/context";
        cmd.aliases = {"ctx"};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdContext(ctx, args);
        };
        RegisterCommand(cmd);
    }

    // /doctor - Diagnostic information
    {
        Command cmd;
        cmd.name = "doctor";
        cmd.description = "Show diagnostic information about the environment";
        cmd.usage = "/doctor";
        cmd.aliases = {};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdDoctor(ctx, args);
        };
        RegisterCommand(cmd);
    }

    // /effort - Effort level control
    {
        Command cmd;
        cmd.name = "effort";
        cmd.description = "Control model effort level and reasoning depth";
        cmd.usage = "/effort [low|medium|high|max|auto]";
        cmd.aliases = {};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdEffort(ctx, args);
        };
        cmd.completer = [](const std::string& partial) {
            std::vector<std::string> levels = {"low", "medium", "high", "max", "auto"};
            std::vector<std::string> completions;
            for (const auto& level : levels) {
                if (level.find(partial) == 0) {
                    completions.push_back(level);
                }
            }
            return completions;
        };
        RegisterCommand(cmd);
    }

    // /plan - Plan mode management
    {
        Command cmd;
        cmd.name = "plan";
        cmd.description = "Manage plan mode for reviewing changes before execution";
        cmd.usage = "/plan [enter|exit|add|approve|reject|list|clear]";
        cmd.aliases = {};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdPlan(ctx, args);
        };
        cmd.completer = [](const std::string& partial) {
            std::vector<std::string> subcommands = {"enter", "exit", "add", "approve", "reject", "list", "clear"};
            std::vector<std::string> completions;
            for (const auto& subcmd : subcommands) {
                if (subcmd.find(partial) == 0) {
                    completions.push_back(subcmd);
                }
            }
            return completions;
        };
        RegisterCommand(cmd);
    }

    // /auto-commit - Auto-generate commit message from diff
    {
        Command cmd;
        cmd.name = "auto-commit";
        cmd.description = "Automatically generate commit message and commit";
        cmd.usage = "/auto-commit [--review] [-a]";
        cmd.aliases = {"ac"};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdAutoCommit(ctx, args);
        };
        cmd.completer = [](const std::string& partial) {
            std::vector<std::string> opts = {"--review", "-a", "--all"};
            std::vector<std::string> completions;
            for (const auto& opt : opts) {
                if (opt.find(partial) == 0) {
                    completions.push_back(opt);
                }
            }
            return completions;
        };
        RegisterCommand(cmd);
    }

    // /memory - Memory management
    {
        Command cmd;
        cmd.name = "memory";
        cmd.description = "Manage long-term memory and notes";
        cmd.usage = "/memory [list|add|search|delete]";
        cmd.aliases = {};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdMemory(ctx, args);
        };
        cmd.completer = [](const std::string& partial) {
            std::vector<std::string> subcommands = {"list", "add", "search", "delete"};
            std::vector<std::string> completions;
            for (const auto& subcmd : subcommands) {
                if (subcmd.find(partial) == 0) {
                    completions.push_back(subcmd);
                }
            }
            return completions;
        };
        RegisterCommand(cmd);
    }

    // /summary - Generate session summary
    {
        Command cmd;
        cmd.name = "summary";
        cmd.description = "Generate a summary of the current session";
        cmd.usage = "/summary [--brief|--detailed]";
        cmd.aliases = {"summarize"};
        cmd.handler = [this](const CommandContext& ctx, const std::vector<std::string>& args) {
            return CmdSummary(ctx, args);
        };
        cmd.completer = [](const std::string& partial) {
            std::vector<std::string> opts = {"--brief", "--detailed"};
            std::vector<std::string> completions;
            for (const auto& opt : opts) {
                if (opt.find(partial) == 0) {
                    completions.push_back(opt);
                }
            }
            return completions;
        };
        RegisterCommand(cmd);
    }

    LOG_INFO("CommandRegistry initialized with {} commands", commands_.size());
}

void CommandRegistry::RegisterCommand(const Command& cmd) {
    commands_[cmd.name] = cmd;
    for (const auto& alias : cmd.aliases) {
        commands_[alias] = cmd;
    }
    LOG_DEBUG("Registered command: {}", cmd.name);
}

CommandResult CommandRegistry::ExecuteCommand(const std::string& name,
                                               const std::vector<std::string>& args,
                                               const CommandContext& ctx) {
    auto it = commands_.find(name);
    if (it == commands_.end()) {
        return CommandResult::Fail("Unknown command: " + name);
    }

    if (it->second.requires_args && args.empty()) {
        return CommandResult::Fail("Command requires arguments: " + it->second.usage);
    }

    try {
        return it->second.handler(ctx, args);
    } catch (const std::exception& e) {
        return CommandResult::Fail(std::string("Error: ") + e.what());
    }
}

bool CommandRegistry::HasCommand(const std::string& name) const {
    return commands_.find(name) != commands_.end();
}

const Command* CommandRegistry::GetCommand(const std::string& name) const {
    auto it = commands_.find(name);
    return (it != commands_.end()) ? &it->second : nullptr;
}

std::vector<std::string> CommandRegistry::GetCommandNames() const {
    std::vector<std::string> names;
    for (const auto& [name, cmd] : commands_) {
        // Only return primary names, not aliases
        if (cmd.name == name) {
            names.push_back(name);
        }
    }
    return names;
}

std::vector<std::pair<std::string, std::string>> CommandRegistry::GetCommandDescriptions() const {
    std::vector<std::pair<std::string, std::string>> result;
    std::unordered_map<std::string, bool> seen;

    for (const auto& [name, cmd] : commands_) {
        if (!seen[cmd.name]) {
            result.push_back({cmd.name, cmd.description});
            seen[cmd.name] = true;
        }
    }

    // Sort by name
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> CommandRegistry::CompleteCommand(const std::string& partial) const {
    std::vector<std::string> completions;

    for (const auto& [name, cmd] : commands_) {
        // Only suggest primary names
        if (cmd.name != name) continue;

        if (name.find(partial) == 0) {
            completions.push_back(name);
        }
    }

    return completions;
}

std::vector<std::string> CommandRegistry::CompleteArguments(const std::string& cmd_name,
                                                             const std::string& partial_arg) const {
    auto it = commands_.find(cmd_name);
    if (it == commands_.end() || !it->second.completer) {
        return {};
    }

    return it->second.completer(partial_arg);
}

/// Comprehensive tab completion for command input
std::vector<std::string> CommandRegistry::CompleteInput(const std::string& input,
                                                          size_t cursor_pos) const {
    if (input.empty()) {
        // At start, show all commands
        return GetCommandNames();
    }

    // Parse input to get tokens up to cursor position
    std::string input_before_cursor = input.substr(0, cursor_pos);
    std::vector<std::string> tokens = ParseCommandLine(input_before_cursor);

    if (tokens.empty()) {
        return GetCommandNames();
    }

    // Check if last token is empty (cursor after space)
    bool trailing_space = input_before_cursor.size() > 0 &&
                          (input_before_cursor.back() == ' ' || input_before_cursor.back() == '\t');

    // First token = command completion
    if (tokens.size() == 1 && !trailing_space) {
        return CompleteCommand(tokens[0]);
    }

    // After first token, check if it's a valid command
    std::string cmd_name = tokens[0];
    // Remove leading '/' or other prefix if present
    if (!cmd_name.empty() && (cmd_name[0] == '/' || cmd_name[0] == ':')) {
        cmd_name = cmd_name.substr(1);
    }

    auto it = commands_.find(cmd_name);
    if (it == commands_.end()) {
        return {};  // Unknown command, no argument completion
    }

    // Get last token for argument completion
    std::string last_token;
    if (!trailing_space && !tokens.empty()) {
        last_token = tokens.back();
    }

    // Use command-specific completer if available
    if (it->second.completer && !last_token.empty()) {
        return it->second.completer(last_token);
    }

    // Default: no completion for arguments without completer
    return {};
}

std::vector<std::string> CommandRegistry::ParseCommandLine(const std::string& line) {
    std::vector<std::string> args;
    std::string current;
    bool in_quotes = false;
    char quote_char = '"';

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (c == '\\' && i + 1 < line.size()) {
            current += line[++i];
            continue;
        }

        if (c == '"' || c == '\'') {
            if (!in_quotes) {
                in_quotes = true;
                quote_char = c;
            } else if (c == quote_char) {
                in_quotes = false;
            } else {
                current += c;
            }
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(c)) && !in_quotes) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        args.push_back(current);
    }

    return args;
}

std::string CommandRegistry::GetHelpText(const std::string& cmd_name) const {
    auto it = commands_.find(cmd_name);
    if (it == commands_.end()) {
        return "Unknown command: " + cmd_name;
    }

    const Command& cmd = it->second;
    std::ostringstream oss;

    oss << "/" << cmd.name << "\n";
    oss << "  " << cmd.description << "\n\n";
    oss << "Usage:\n";
    oss << "  " << cmd.usage << "\n";

    if (!cmd.aliases.empty()) {
        oss << "\nAliases: /" << cmd.aliases[0];
        for (size_t i = 1; i < cmd.aliases.size() && i < 3; ++i) {
            oss << ", /" << cmd.aliases[i];
        }
        oss << "\n";
    }

    return oss.str();
}

std::string CommandRegistry::GetAllCommandsHelp() const {
    std::ostringstream oss;
    oss << "Available commands:\n\n";

    auto descriptions = GetCommandDescriptions();
    for (const auto& [name, desc] : descriptions) {
        oss << "  /" << name;

        // Pad for alignment
        size_t padding = 20 - name.length();
        if (padding > 0) oss << std::string(padding, ' ');

        oss << desc << "\n";
    }

    oss << "\nUse /help <command> for more information.\n";
    return oss.str();
}

// ==================== Built-in Command Handlers ====================

CommandResult CommandRegistry::CmdHelp(const CommandContext&, const std::vector<std::string>& args) {
    if (args.empty()) {
        return CommandResult::Ok(GetAllCommandsHelp());
    }

    return CommandResult::Ok(GetHelpText(args[0]));
}

CommandResult CommandRegistry::CmdCost(const CommandContext&, const std::vector<std::string>& args) {
    auto& tracker = TokenTracker::GetInstance();

    std::ostringstream oss;
    oss << "=== Token Usage Statistics ===\n\n";

    if (args.empty()) {
        // Show total stats
        oss << tracker.FormatTotalCost();
        oss << "\n";
        oss << tracker.FormatModelUsage();
    } else {
        // Show stats for specific model
        std::string model = args[0];
        auto stats = tracker.GetModelStats(model);

        if (stats.total_tokens == 0) {
            return CommandResult::Ok("No token usage recorded for model: " + model);
        }

        oss << "Model: " << model << "\n";
        oss << "  Prompt tokens: " << stats.prompt_tokens << "\n";
        oss << "  Completion tokens: " << stats.completion_tokens << "\n";
        oss << "  Total tokens: " << stats.total_tokens << "\n";
        oss << "  Cost: $" << std::fixed << std::setprecision(4) << stats.cost_usd << "\n";
    }

    return CommandResult::Ok(oss.str());
}

CommandResult CommandRegistry::CmdStatus(const CommandContext& ctx, const std::vector<std::string>& args) {
    std::string path = args.empty() ? ctx.workspace : args[0];

    std::string cmd = "cd \"" + path + "\" && git status 2>&1";
    std::ostringstream result;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return CommandResult::Fail("Failed to execute git status");
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result << buffer;
    }

    int status = pclose(pipe);
    if (status != 0) {
        return CommandResult::Fail("Not a git repository or git not available");
    }

    return CommandResult::Ok(result.str());
}

CommandResult CommandRegistry::CmdDiff(const CommandContext& ctx, const std::vector<std::string>& args) {
    std::string path = ctx.workspace;
    bool cached = false;

    for (const auto& arg : args) {
        if (arg == "--cached" || arg == "-c") {
            cached = true;
        } else if (!arg.empty() && arg[0] != '-') {
            path = arg;
        }
    }

    std::string cmd = "cd \"" + path + "\" && git diff ";
    if (cached) cmd += "--cached ";
    cmd += "2>&1";

    std::ostringstream result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return CommandResult::Fail("Failed to execute git diff");
    }

    char buffer[4096];
    bool has_diff = false;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result << buffer;
        has_diff = true;
    }
    pclose(pipe);

    if (!has_diff) {
        return CommandResult::Ok(cached ? "No staged changes." : "No working directory changes.");
    }

    return CommandResult::Ok(result.str());
}

CommandResult CommandRegistry::CmdCommit(const CommandContext&, const std::vector<std::string>& args) {
    bool do_all = false;
    std::string message;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-a" || args[i] == "--all") {
            do_all = true;
        } else if ((args[i] == "-m" || args[i] == "--message") && i + 1 < args.size()) {
            message = args[++i];
            // Remove quotes if present
            if ((message.front() == '"' && message.back() == '"') ||
                (message.front() == '\'' && message.back() == '\'')) {
                message = message.substr(1, message.size() - 2);
            }
        }
    }

    if (message.empty()) {
        return CommandResult::Fail("Commit message is required (-m \"message\")");
    }

    std::string cmd = "git commit ";
    if (do_all) cmd += "-a ";
    cmd += "-m \"" + message + "\" 2>&1";

    std::ostringstream result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return CommandResult::Fail("Failed to execute git commit");
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result << buffer;
    }
    int status = pclose(pipe);

    if (status != 0) {
        std::string output = result.str();
        if (output.find("nothing to commit") != std::string::npos) {
            return CommandResult::Ok("Nothing to commit, working tree clean.");
        }
        return CommandResult::Fail(output);
    }

    return CommandResult::Ok(result.str());
}

CommandResult CommandRegistry::CmdTasks(const CommandContext&, const std::vector<std::string>& args) {
    auto& task_mgr = TaskManager::GetInstance();
    std::ostringstream oss;

    if (args.empty() || args[0] == "list" || args[0] == "ls") {
        // List tasks
        auto tasks = task_mgr.GetAllTasks();

        if (tasks.empty()) {
            return CommandResult::Ok("No tasks found. Use /tasks add <subject> to create one.");
        }

        oss << "Tasks:\n\n";
        for (const auto& task : tasks) {
            std::string status_icon;
            switch (task.status) {
                case TaskStatus::Pending: status_icon = "[ ]"; break;
                case TaskStatus::InProgress: status_icon = "[~]"; break;
                case TaskStatus::Completed: status_icon = "[x]"; break;
                case TaskStatus::Cancelled: status_icon = "[-]"; break;
                case TaskStatus::Failed: status_icon = "[!]"; break;
            }

            oss << "  " << status_icon << " " << task.id << ": " << task.subject;
            if (!task.owner.empty()) oss << " (@" << task.owner << ")";
            oss << "\n";
        }

        oss << "\n" << task_mgr.GetTotalTaskCount() << " total tasks\n";
        oss << "  Pending: " << task_mgr.GetTaskCount(TaskStatus::Pending) << "\n";
        oss << "  In Progress: " << task_mgr.GetTaskCount(TaskStatus::InProgress) << "\n";
        oss << "  Completed: " << task_mgr.GetTaskCount(TaskStatus::Completed) << "\n";

        return CommandResult::Ok(oss.str());
    }

    if (args[0] == "add" || args[0] == "create") {
        if (args.size() < 2) {
            return CommandResult::Fail("Usage: /tasks add <subject>");
        }

        std::string subject = args[1];
        std::string description = (args.size() > 2) ? args[2] : "";
        std::string id = task_mgr.CreateTask(subject, description);

        return CommandResult::Ok("Created task: " + id + "\n  Subject: " + subject);
    }

    if (args[0] == "complete" || args[0] == "done") {
        if (args.size() < 2) {
            return CommandResult::Fail("Usage: /tasks complete <task_id>");
        }

        if (task_mgr.UpdateTaskStatus(args[1], TaskStatus::Completed)) {
            return CommandResult::Ok("Completed task: " + args[1]);
        }
        return CommandResult::Fail("Task not found: " + args[1]);
    }

    if (args[0] == "delete" || args[0] == "rm") {
        if (args.size() < 2) {
            return CommandResult::Fail("Usage: /tasks delete <task_id>");
        }

        if (task_mgr.DeleteTask(args[1])) {
            return CommandResult::Ok("Deleted task: " + args[1]);
        }
        return CommandResult::Fail("Task not found: " + args[1]);
    }

    if (args[0] == "start") {
        if (args.size() < 2) {
            return CommandResult::Fail("Usage: /tasks start <task_id>");
        }

        if (task_mgr.UpdateTaskStatus(args[1], TaskStatus::InProgress)) {
            return CommandResult::Ok("Started task: " + args[1]);
        }
        return CommandResult::Fail("Task not found: " + args[1]);
    }

    return CommandResult::Fail("Unknown tasks subcommand. Use: list, add, complete, start, delete");
}

CommandResult CommandRegistry::CmdConfig(const CommandContext&, const std::vector<std::string>& args) {
    auto& config = AiCodeConfig::GetInstance();

    if (args.empty()) {
        // Show current config
        std::ostringstream oss;
        oss << "Current configuration:\n";
        oss << "  Default provider: " << config.default_provider << "\n";
        oss << "  Default agent: " << config.default_agent << "\n";
        oss << "  Log level: " << config.log_level << "\n";
        oss << "  Permission level: " << config.security.permission_level << "\n";
        return CommandResult::Ok(oss.str());
    }

    return CommandResult::Ok("Config editing not yet implemented. Please edit ~/.aicode/config.json directly.");
}

CommandResult CommandRegistry::CmdClear(const CommandContext&, const std::vector<std::string>&) {
    // Send ANSI clear screen code
    return CommandResult::Ok("\033[2J\033[H");
}

CommandResult CommandRegistry::CmdMcp(const CommandContext&, const std::vector<std::string>& args) {
    auto& mcp_client = aicode::McpClient::GetInstance();
    std::ostringstream oss;

    if (args.empty() || args[0] == "list") {
        // List MCP servers and tools
        auto tools = mcp_client.GetAvailableTools();
        auto resources = mcp_client.GetAvailableResources();
        auto servers = mcp_client.GetConfiguredServers();

        oss << "=== MCP Status ===\n\n";

        if (tools.empty() && resources.empty() && servers.empty()) {
            oss << "No MCP servers configured.\n";
            oss << "\nUse /mcp add <name> to add a server.\n";
        } else {
            oss << "Configured Servers (" << servers.size() << "):\n";
            for (const auto& server : servers) {
                oss << "  - " << server.name << " (type: " << server.type << ")\n";
            }
            oss << "\n";

            if (!tools.empty()) {
                oss << "Tools (" << tools.size() << "):\n";
                for (const auto& tool : tools) {
                    oss << "  - " << tool.name << " (server: " << tool.server_name << ")\n";
                }
                oss << "\n";
            }

            if (!resources.empty()) {
                oss << "Resources (" << resources.size() << "):\n";
                for (const auto& res : resources) {
                    oss << "  - " << res.name << " (" << res.mime_type << ")\n";
                }
            }
        }

        return CommandResult::Ok(oss.str());
    }

    if (args[0] == "status") {
        // Show connection status
        auto tools = mcp_client.GetAvailableTools();
        auto resources = mcp_client.GetAvailableResources();
        oss << "MCP Client Status:\n";
        oss << "  Servers configured: " << tools.size() << "\n";
        oss << "  Tools available: " << tools.size() << "\n";
        oss << "  Resources available: " << resources.size() << "\n";
        return CommandResult::Ok(oss.str());
    }

    if (args[0] == "add" && args.size() >= 2) {
        // Add a new MCP server: /mcp add <name> [--command <cmd>] [--args <args...] [--type stdio]
        std::string server_name = args[1];
        std::string type = "stdio";
        std::string command;
        std::vector<std::string> cmd_args;

        for (size_t i = 2; i < args.size(); i++) {
            if (args[i] == "--type" && i + 1 < args.size()) {
                type = args[++i];
            } else if (args[i] == "--command" && i + 1 < args.size()) {
                command = args[++i];
            } else if (args[i] == "--args" && i + 1 < args.size()) {
                cmd_args.push_back(args[++i]);
            }
        }

        if (command.empty()) {
            return CommandResult::Fail("Command required for stdio MCP servers. Use: /mcp add <name> --command <cmd>");
        }

        aicode::McpServerConfig config;
        config.name = server_name;
        config.type = type;
        config.command = command;
        config.args = cmd_args;
        config.enabled = true;

        if (mcp_client.AddServer(config)) {
            oss << "Added MCP server: " << server_name << "\n";
            oss << "  Type: " << type << "\n";
            oss << "  Command: " << command;
            if (!cmd_args.empty()) {
                for (const auto& arg : cmd_args) {
                    oss << " " << arg;
                }
            }
            oss << "\n";
            return CommandResult::Ok(oss.str());
        } else {
            return CommandResult::Fail("Failed to add MCP server: " + server_name);
        }
    }

    if (args[0] == "remove" && args.size() >= 2) {
        std::string server_name = args[1];
        if (mcp_client.RemoveServer(server_name)) {
            return CommandResult::Ok("Removed MCP server: " + server_name);
        } else {
            return CommandResult::Fail("Failed to remove MCP server: " + server_name);
        }
    }

    return CommandResult::Fail("Unknown mcp subcommand. Use: list, status, add <name>, remove <name>");
}

CommandResult CommandRegistry::CmdResume(const CommandContext&, const std::vector<std::string>& args) {
    auto& session_mgr = SessionManager::GetInstance();
    std::ostringstream oss;

    std::string session_id;
    if (!args.empty()) {
        session_id = args[0];
    } else {
        session_id = session_mgr.GetLastSessionId();
        if (session_id.empty()) {
            return CommandResult::Fail("No previous session found.");
        }
    }

    std::vector<MessageSchema> messages;
    nlohmann::json metadata;

    if (session_mgr.LoadSession(session_id, messages, metadata)) {
        oss << "Resumed session: " << session_id << "\n";
        oss << "  Messages: " << messages.size() << "\n";
        oss << "  Created: " << metadata.value("created_at", "unknown") << "\n";
        if (metadata.contains("workspace")) {
            oss << "  Workspace: " << metadata["workspace"] << "\n";
        }
        if (metadata.contains("last_user_message")) {
            oss << "  Last message: " << metadata["last_user_message"] << "\n";
        }
        return CommandResult::Ok(oss.str());
    }

    return CommandResult::Fail("Failed to load session: " + session_id);
}

CommandResult CommandRegistry::CmdSessions(const CommandContext&, const std::vector<std::string>&) {
    auto& session_mgr = SessionManager::GetInstance();
    auto sessions = session_mgr.ListSessions();
    std::ostringstream oss;

    if (sessions.empty()) {
        return CommandResult::Ok("No saved sessions found.");
    }

    oss << "Saved sessions:\n\n";
    for (size_t i = 0; i < sessions.size() && i < 20; ++i) {
        const auto& s = sessions[i];
        std::string marker = (i == 0) ? "* " : "  ";
        oss << marker << s.session_id << "\n";
        oss << "       " << s.message_count << " messages, "
            << s.token_count << " tokens, $"
            << std::fixed << std::setprecision(2) << s.cost_usd << "\n";
        oss << "       Updated: " << s.updated_at << "\n";
        if (!s.last_user_message.empty()) {
            oss << "       Last: " << s.last_user_message.substr(0, 50);
            if (s.last_user_message.size() > 50) oss << "...";
            oss << "\n";
        }
        oss << "\n";
    }

    if (sessions.size() > 20) {
        oss << "... and " << (sessions.size() - 20) << " more sessions.\n";
    }

    return CommandResult::Ok(oss.str());
}

CommandResult CommandRegistry::CmdCompact(const CommandContext&, const std::vector<std::string>&) {
    // This command would trigger context compaction
    // The actual implementation depends on AgentCore integration
    return CommandResult::Ok("Context compaction requested. Use this in a session to compact history.");
}

CommandResult CommandRegistry::CmdPlugins(const CommandContext&, const std::vector<std::string>& args) {
    auto& plugin_mgr = aicode::PluginManager::GetInstance();
    std::ostringstream oss;

    if (args.empty() || args[0] == "list") {
        auto plugins = plugin_mgr.GetAllPlugins();
        if (plugins.empty()) {
            return CommandResult::Ok("No plugins installed.\n\nInstall plugins by placing them in: " + plugin_mgr.GetPluginsDir());
        }

        oss << "Installed plugins:\n\n";
        for (const auto& p : plugins) {
            std::string status = p.enabled ? "[x]" : "[ ]";
            oss << "  " << status << " " << p.name << " v" << p.version << "\n";
            if (!p.description.empty()) {
                oss << "      " << p.description << "\n";
            }
            oss << "      Commands: " << p.commands.size() << ", Agents: " << p.agents.size() << ", Skills: " << p.skills.size() << "\n";
        }
        return CommandResult::Ok(oss.str());
    }

    if (args[0] == "enable" && args.size() > 1) {
        if (plugin_mgr.EnablePlugin(args[1])) {
            return CommandResult::Ok("Enabled plugin: " + args[1]);
        }
        return CommandResult::Fail("Plugin not found: " + args[1]);
    }

    if (args[0] == "disable" && args.size() > 1) {
        if (plugin_mgr.DisablePlugin(args[1])) {
            return CommandResult::Ok("Disabled plugin: " + args[1]);
        }
        return CommandResult::Fail("Plugin not found: " + args[1]);
    }

    if (args[0] == "reload") {
        plugin_mgr.ReloadPlugins();
        return CommandResult::Ok("Plugins reloaded.");
    }

    return CommandResult::Fail("Unknown plugins subcommand. Use: list, enable, disable, reload");
}

CommandResult CommandRegistry::CmdSkills(const CommandContext&, const std::vector<std::string>& args) {
    auto& plugin_mgr = aicode::PluginManager::GetInstance();
    std::ostringstream oss;

    if (args.empty() || args[0] == "list") {
        auto skills = plugin_mgr.GetAllSkills();
        if (skills.empty()) {
            return CommandResult::Ok("No skills available.");
        }

        oss << "Available skills:\n\n";
        for (const auto& s : skills) {
            oss << "  - " << s << "\n";
        }
        return CommandResult::Ok(oss.str());
    }

    return CommandResult::Fail("Unknown skills subcommand. Use: list");
}

CommandResult CommandRegistry::CmdWorktree(const CommandContext&, const std::vector<std::string>& args) {
    auto& wt_mgr = aicode::WorktreeManager::GetInstance();
    std::ostringstream oss;

    if (args.empty() || args[0] == "list" || args[0] == "ls") {
        auto worktrees = wt_mgr.ListWorktrees();
        if (worktrees.empty()) {
            oss << "No worktrees found.\n\n";
            oss << "Create one with: /worktree new <name> [branch]\n";
        } else {
            oss << "Git worktrees:\n\n";
            for (const auto& wt : worktrees) {
                oss << "  " << wt.name << "\n";
                oss << "    Path: " << wt.path << "\n";
                oss << "    Branch: " << wt.branch << "\n";
                if (!wt.head_commit.empty()) {
                    oss << "    HEAD: " << wt.head_commit << "\n";
                }
                if (wt.is_locked) {
                    oss << "    Locked: " << wt.locked_reason << "\n";
                }
                oss << "\n";
            }
        }
        return CommandResult::Ok(oss.str());
    }

    if (args[0] == "new" || args[0] == "create") {
        if (args.size() < 2) {
            return CommandResult::Fail("Usage: /worktree new <name> [branch]");
        }

        std::string name = args[1];
        std::string branch = (args.size() > 2) ? args[2] : "";

        auto info = wt_mgr.Create(name, "", branch);
        if (info.path.empty()) {
            return CommandResult::Fail("Failed to create worktree. Check if name already exists.");
        }

        oss << "Created worktree: " << name << "\n";
        oss << "  Path: " << info.path << "\n";
        if (!branch.empty()) {
            oss << "  Branch: " << info.branch << "\n";
        }
        oss << "\nSwitch to it with: /worktree switch " << name << "\n";
        return CommandResult::Ok(oss.str());
    }

    if (args[0] == "switch" || args[0] == "cd") {
        if (args.size() < 2) {
            return CommandResult::Fail("Usage: /worktree switch <name>");
        }

        std::string path = wt_mgr.SwitchWorktree(args[1]);
        if (path.empty()) {
            return CommandResult::Fail("Worktree not found: " + args[1]);
        }

        return CommandResult::Ok("Switched to worktree: " + args[1] + "\n  Path: " + path);
    }

    if (args[0] == "remove" || args[0] == "rm" || args[0] == "delete") {
        if (args.size() < 2) {
            return CommandResult::Fail("Usage: /worktree remove <name> [--force]");
        }

        bool force = (args.size() > 2 && (args[2] == "--force" || args[2] == "-f"));
        if (!wt_mgr.Remove(args[1], force, false)) {
            return CommandResult::Fail("Failed to remove worktree. Use --force to override.");
        }

        return CommandResult::Ok("Removed worktree: " + args[1]);
    }

    if (args[0] == "prune") {
        int pruned = wt_mgr.PruneStaleWorktrees();
        return CommandResult::Ok("Pruned " + std::to_string(pruned) + " stale worktrees.");
    }

    return CommandResult::Fail("Unknown worktree subcommand. Use: list, new, switch, remove, prune");
}

CommandResult CommandRegistry::CmdSchedule(const CommandContext&, const std::vector<std::string>& args) {
    auto& cron = aicode::CronScheduler::GetInstance();
    std::ostringstream oss;

    if (args.empty() || args[0] == "list" || args[0] == "ls") {
        auto tasks = cron.ListTasks();
        if (tasks.empty()) {
            oss << "No scheduled tasks.\n\n";
            oss << "Create one with: /schedule create \"*/5 * * * *\" \"check for updates\"\n";
        } else {
            oss << "Scheduled tasks:\n\n";
            for (const auto& task : tasks) {
                std::string status_icon;
                switch (task.status) {
                    case CronJobStatus::Active: status_icon = "[x]"; break;
                    case CronJobStatus::Paused: status_icon = "[ ]"; break;
                    case CronJobStatus::Completed: status_icon = "[-]"; break;
                    case CronJobStatus::Failed: status_icon = "[!]"; break;
                }
                oss << "  " << status_icon << " " << task.id << "\n";
                oss << "    Cron: " << task.cron_expression << "\n";
                oss << "    Prompt: " << task.prompt << "\n";
                oss << "    Recurring: " << (task.recurring ? "yes" : "no") << "\n";
                oss << "    Executions: " << task.execution_count;
                if (task.max_executions > 0) {
                    oss << "/" << task.max_executions;
                }
                oss << "\n";
                oss << "    Next run: " << task.next_execution_at << "\n";
                if (!task.last_result.empty()) {
                    oss << "    Last result: " << task.last_result.substr(0, 50);
                    if (task.last_result.size() > 50) oss << "...";
                    oss << "\n";
                }
                oss << "\n";
            }
        }
        return CommandResult::Ok(oss.str());
    }

    if (args[0] == "create" || args[0] == "new") {
        if (args.size() < 3) {
            return CommandResult::Fail("Usage: /schedule create \"<cron>\" \"<prompt>\" [--recurring] [--durable]");
        }

        std::string cron_expr = args[1];
        std::string prompt = args[2];
        bool recurring = true;
        bool durable = false;

        for (size_t i = 3; i < args.size(); ++i) {
            if (args[i] == "--one-shot" || args[i] == "--once") {
                recurring = false;
            } else if (args[i] == "--durable" || args[i] == "--persist") {
                durable = true;
            }
        }

        std::string task_id = cron.Schedule(cron_expr, prompt, recurring, durable);
        if (task_id.empty()) {
            return CommandResult::Fail("Failed to schedule task.");
        }

        oss << "Scheduled task: " << task_id << "\n";
        oss << "  Cron: " << cron_expr << "\n";
        oss << "  Prompt: " << prompt << "\n";
        oss << "  Recurring: " << (recurring ? "yes" : "no") << "\n";
        oss << "  Durable: " << (durable ? "yes" : "no") << "\n";
        return CommandResult::Ok(oss.str());
    }

    if (args[0] == "delete" || args[0] == "rm") {
        if (args.size() < 2) {
            return CommandResult::Fail("Usage: /schedule delete <task_id>");
        }

        if (cron.Delete(args[1])) {
            return CommandResult::Ok("Deleted task: " + args[1]);
        }
        return CommandResult::Fail("Task not found: " + args[1]);
    }

    if (args[0] == "run" || args[0] == "execute") {
        if (args.size() < 2) {
            return CommandResult::Fail("Usage: /schedule run <task_id>");
        }

        std::string result = cron.RunNow(args[1]);
        if (result.empty()) {
            return CommandResult::Fail("Task not found or execution failed.");
        }
        return CommandResult::Ok("Task execution result:\n" + result);
    }

    if (args[0] == "pause") {
        if (args.size() < 2) {
            return CommandResult::Fail("Usage: /schedule pause <task_id>");
        }

        if (cron.Pause(args[1])) {
            return CommandResult::Ok("Paused task: " + args[1]);
        }
        return CommandResult::Fail("Task not found: " + args[1]);
    }

    if (args[0] == "resume") {
        if (args.size() < 2) {
            return CommandResult::Fail("Usage: /schedule resume <task_id>");
        }

        if (cron.Resume(args[1])) {
            return CommandResult::Ok("Resumed task: " + args[1]);
        }
        return CommandResult::Fail("Task not found: " + args[1]);
    }

    return CommandResult::Fail("Unknown schedule subcommand. Use: list, create, delete, run, pause, resume");
}

CommandResult CommandRegistry::CmdContext(const CommandContext&, const std::vector<std::string>&) {
    auto& tracker = TokenTracker::GetInstance();
    auto& compact = CompactService::GetInstance();

    std::ostringstream oss;
    oss << "=== Context Usage ===\n\n";

    // Token usage
    oss << "Token Statistics:\n";
    oss << tracker.FormatTotalCost();
    oss << "\n";

    // Model-specific usage
    oss << tracker.FormatModelUsage();
    oss << "\n";

    // Cache usage
    auto extended = tracker.GetExtendedTotalStats();
    oss << "Cache Usage:\n";
    oss << "  Cache read tokens: " << extended.cache_read_input_tokens << "\n";
    oss << "  Cache creation tokens: " << extended.cache_creation_input_tokens << "\n";
    oss << "\n";

    // Code changes
    oss << "Code Changes:\n";
    oss << "  Lines added: " << extended.lines_added << "\n";
    oss << "  Lines removed: " << extended.lines_removed << "\n";
    oss << "\n";

    // Web searches
    oss << "Web Searches: " << extended.web_search_requests << " requests\n\n";

    // Compact service stats
    oss << "Context Compaction:\n";
    oss << "  Compactions performed: " << compact.GetCompactionCount() << "\n";
    oss << "  Auto-compaction: " << (compact.IsAutoCompactEnabled() ? "enabled" : "disabled") << "\n";

    return CommandResult::Ok(oss.str());
}

CommandResult CommandRegistry::CmdDoctor(const CommandContext& ctx, const std::vector<std::string>&) {
    auto& tracker = TokenTracker::GetInstance();
    auto& perm_mgr = PermissionManager::GetInstance();
    auto& mcp_client = aicode::McpClient::GetInstance();
    auto& lsp_manager = aicode::LspManager::GetInstance();

    std::ostringstream oss;
    oss << "=== AiCode Diagnostic Information ===\n\n";

    // Version and environment
    oss << "Environment:\n";
    oss << "  AiCode Version: " << AICODE_VERSION << "\n";
    oss << "  Workspace: " << ctx.workspace << "\n";
#ifdef NDEBUG
    oss << "  Build: Release\n";
#else
    oss << "  Build: Debug\n";
#endif
    oss << "\n";

    // Git status
    oss << "Git Status:\n";
    std::string cmd = "cd \"" + ctx.workspace + "\" && git rev-parse --git-dir 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        char buffer[256];
        std::string git_dir;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            git_dir += buffer;
        }
        pclose(pipe);

        if (!git_dir.empty() && git_dir.find("fatal:") == std::string::npos) {
            oss << "  ✓ Git repository detected\n";

            // Get current branch
            cmd = "cd \"" + ctx.workspace + "\" && git branch --show-current 2>&1";
            pipe = popen(cmd.c_str(), "r");
            if (pipe) {
                char buf[256];
                if (fgets(buf, sizeof(buf), pipe) != nullptr) {
                    oss << "  Branch: " << buf << "\n";
                }
                pclose(pipe);
            }

            // Get recent commit
            cmd = "cd \"" + ctx.workspace + "\" && git log -1 --oneline 2>&1";
            pipe = popen(cmd.c_str(), "r");
            if (pipe) {
                char buf[256];
                if (fgets(buf, sizeof(buf), pipe) != nullptr) {
                    oss << "  HEAD: " << buf;
                }
                pclose(pipe);
            }
        } else {
            oss << "  ⚠ Not a git repository\n";
        }
    }
    oss << "\n";

    // Permission status
    oss << "Permission System:\n";
    oss << "  Mode: " << perm_mgr.GetMode() << "\n";
    auto allow_rules = perm_mgr.GetAllowRules();
    auto deny_rules = perm_mgr.GetDenyRules();
    auto ask_rules = perm_mgr.GetAskRules();
    oss << "  Allow rules: " << allow_rules.size() << "\n";
    oss << "  Deny rules: " << deny_rules.size() << "\n";
    oss << "  Ask rules: " << ask_rules.size() << "\n";
    oss << "\n";

    // MCP status
    oss << "MCP Integration:\n";
    auto tools = mcp_client.GetAvailableTools();
    auto resources = mcp_client.GetAvailableResources();
    if (tools.empty() && resources.empty()) {
        oss << "  ⚠ No MCP servers configured\n";
    } else {
        oss << "  ✓ MCP servers connected\n";
        oss << "  Tools: " << tools.size() << "\n";
        oss << "  Resources: " << resources.size() << "\n";
    }
    oss << "\n";

    // LSP status
    oss << "LSP Integration:\n";
    auto servers = lsp_manager.GetRegisteredServers();
    if (servers.empty()) {
        oss << "  ⚠ No LSP servers registered\n";
    } else {
        oss << "  ✓ LSP servers registered: " << servers.size() << "\n";
        for (const auto& s : servers) {
            oss << "    - " << s << "\n";
        }
    }
    oss << "\n";

    // Token usage summary
    oss << "Token Usage Summary:\n";
    auto stats = tracker.GetTotalStats();
    oss << "  Total tokens: " << stats.total_tokens << "\n";
    oss << "  Total cost: $" << std::fixed << std::setprecision(4) << stats.cost_usd << "\n";
    oss << "\n";

    // Configuration
    oss << "Configuration:\n";
    auto& config = AiCodeConfig::GetInstance();
    oss << "  Default provider: " << config.default_provider << "\n";
    oss << "  Log level: " << config.log_level << "\n";

    return CommandResult::Ok(oss.str());
}

CommandResult CommandRegistry::CmdEffort(const CommandContext&, const std::vector<std::string>& args) {
    if (args.empty()) {
        // Show current effort level
        auto current = EffortConfig::GetCurrent();
        std::ostringstream oss;
        oss << "Current effort level: " << EffortConfig::ToString(current) << "\n";
        oss << "\n" << EffortConfig::Description(current) << "\n";
        oss << "\nValid levels:\n";
        for (const auto& level : EffortConfig::GetValidLevels()) {
            oss << "  " << level << " - " << EffortConfig::Description(EffortConfig::ParseLevel(level)) << "\n";
        }
        return CommandResult::Ok(oss.str());
    }

    // Set new effort level
    std::string level_str = args[0];
    if (!EffortConfig::IsValidLevel(level_str)) {
        return CommandResult::Fail("Invalid effort level: " + level_str +
                                   ". Valid levels: low, medium, high, max, auto");
    }

    EffortConfig::SetFromString(level_str);

    std::ostringstream oss;
    oss << "Effort level set to: " << level_str << "\n";
    oss << "\n" << EffortConfig::Description(EffortConfig::ParseLevel(level_str)) << "\n";

    // Note about model compatibility
    if (level_str == "max") {
        oss << "\nNote: 'max' effort requires Claude Opus 4.6 for best results.\n";
    }

    return CommandResult::Ok(oss.str());
}

CommandResult CommandRegistry::CmdPlan(const CommandContext&, const std::vector<std::string>& args) {
    auto& plan_manager = aicode::PlanModeManager::GetInstance();
    std::ostringstream oss;

    if (args.empty()) {
        // Show current plan status
        if (!plan_manager.IsInPlanMode()) {
            oss << "Not in plan mode.\n";
            oss << "\nUse /plan enter to start a new plan.\n";
        } else {
            auto* plan = plan_manager.GetCurrentPlan();
            if (!plan) {
                oss << "In plan mode but no active plan.\n";
            } else {
                oss << plan_manager.GetPlanAsMarkdown();
            }
        }
        return CommandResult::Ok(oss.str());
    }

    const std::string& subcommand = args[0];

    if (subcommand == "enter") {
        plan_manager.EnterPlanMode();
        plan_manager.CreatePlan("New Plan", "Planning mode activated");
        return CommandResult::Ok("Entered plan mode. Use /plan add <description> to add steps.\n"
                                 "Use /plan approve when ready to execute.");
    }

    if (subcommand == "exit") {
        plan_manager.ExitPlanMode();
        return CommandResult::Ok("Exited plan mode.");
    }

    if (subcommand == "add" && args.size() >= 2) {
        if (!plan_manager.IsInPlanMode()) {
            return CommandResult::Fail("Not in plan mode. Use /plan enter first.");
        }
        std::string description = args[1];
        for (size_t i = 2; i < args.size(); i++) {
            description += " " + args[i];
        }
        plan_manager.AddStep(description);
        return CommandResult::Ok("Added step: " + description);
    }

    if (subcommand == "approve") {
        if (!plan_manager.IsInPlanMode()) {
            return CommandResult::Fail("Not in plan mode.");
        }
        plan_manager.ApprovePlan();
        return CommandResult::Ok("Plan approved. Ready to execute.");
    }

    if (subcommand == "reject") {
        if (!plan_manager.IsInPlanMode()) {
            return CommandResult::Fail("Not in plan mode.");
        }
        plan_manager.RejectPlan();
        return CommandResult::Ok("Plan rejected.");
    }

    if (subcommand == "list") {
        if (!plan_manager.IsInPlanMode()) {
            return CommandResult::Fail("Not in plan mode.");
        }
        oss << plan_manager.GetPlanAsMarkdown();
        return CommandResult::Ok(oss.str());
    }

    if (subcommand == "clear") {
        plan_manager.ClearPlan();
        return CommandResult::Ok("Plan cleared.");
    }

    return CommandResult::Fail("Unknown plan subcommand. Use: enter, exit, add, approve, reject, list, clear");
}

CommandResult CommandRegistry::CmdAutoCommit(const CommandContext&, const std::vector<std::string>& args) {
    std::ostringstream oss;
    bool do_review = false;
    bool do_all = false;

    // Parse arguments
    for (const auto& arg : args) {
        if (arg == "--review") {
            do_review = true;
        } else if (arg == "-a" || arg == "--all") {
            do_all = true;
        }
    }

    // Step 1: Get git diff
    std::string diff_cmd = "git diff --cached";
    if (do_all) {
        diff_cmd = "git diff HEAD";
    }
    diff_cmd += " 2>&1";

    FILE* pipe = popen(diff_cmd.c_str(), "r");
    if (!pipe) {
        return CommandResult::Fail("Failed to execute git diff");
    }

    std::string diff_output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        diff_output += buffer;
    }
    pclose(pipe);

    if (diff_output.empty() || diff_output.find("diff --git") == std::string::npos) {
        // Check if there are any changes at all
        std::string status_cmd = "git status --porcelain 2>&1";
        pipe = popen(status_cmd.c_str(), "r");
        std::string status_output;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            status_output += buffer;
        }
        pclose(pipe);

        if (status_output.empty()) {
            return CommandResult::Ok("Nothing to commit, working tree clean.");
        }
    }

    // Step 2: Analyze diff and generate commit message
    oss << "=== Changes Summary ===\n\n";

    // Parse diff to get changed files
    std::set<std::string> changed_files;
    std::vector<std::string> added_lines;
    std::vector<std::string> removed_lines;

    std::istringstream diff_stream(diff_output);
    std::string line;
    while (std::getline(diff_stream, line)) {
        if (line.find("diff --git") == 0) {
            // Extract file path from b/ side
            size_t b_pos = line.find(" b/");
            if (b_pos != std::string::npos) {
                std::string file = line.substr(b_pos + 3);
                changed_files.insert(file);
            }
        } else if (line.find("+++") == 0) {
            oss << "Modified: " << line.substr(4) << "\n";
        } else if (line[0] == '+' && line.size() > 1 && line[1] != '+') {
            added_lines.push_back(line);
        } else if (line[0] == '-' && line.size() > 1 && line[1] != '-') {
            removed_lines.push_back(line);
        }
    }

    oss << "\nStatistics:\n";
    oss << "  Files changed: " << changed_files.size() << "\n";
    oss << "  Lines added: " << added_lines.size() << "\n";
    oss << "  Lines removed: " << removed_lines.size() << "\n";

    // Generate commit message based on changes
    std::string commit_message;

    // Determine commit message based on file types
    bool has_cpp = false, has_header = false, has_md = false, has_test = false;
    for (const auto& file : changed_files) {
        if (file.find(".cc") != std::string::npos || file.find(".cpp") != std::string::npos) {
            has_cpp = true;
        }
        if (file.find(".h") != std::string::npos) {
            has_header = true;
        }
        if (file.find(".md") != std::string::npos) {
            has_md = true;
        }
        if (file.find("test") != std::string::npos || file.find(".test.") != std::string::npos) {
            has_test = true;
        }
    }

    // Generate subject line
    if (has_test) {
        commit_message = "Add tests";
    } else if (has_md) {
        commit_message = "Update documentation";
    } else if (has_header && has_cpp) {
        commit_message = "Update implementation and headers";
    } else if (has_cpp) {
        commit_message = "Update implementation";
    } else {
        commit_message = "Update codebase";
    }

    // Add file list to commit message
    if (changed_files.size() <= 5) {
        for (const auto& file : changed_files) {
            commit_message += " - " + file;
        }
    } else {
        commit_message += " (" + std::to_string(changed_files.size()) + " files)";
    }

    oss << "\n=== Suggested Commit MessageSchema ===\n";
    oss << commit_message << "\n";

    if (do_review) {
        oss << "\nReview mode. Please confirm the changes above.\n";
        oss << "Run '/auto-commit -a' to commit with this message, or '/commit -m \"" << commit_message << "\"' to commit manually.\n";
        return CommandResult::Ok(oss.str());
    }

    // Step 3: Execute commit
    oss << "\n=== Committing ===\n";

    std::string commit_cmd = "git commit ";
    if (do_all) commit_cmd += "-a ";
    commit_cmd += "-m \"" + commit_message + "\" 2>&1";

    pipe = popen(commit_cmd.c_str(), "r");
    std::string commit_output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        commit_output += buffer;
    }
    int status = pclose(pipe);

    if (status != 0) {
        if (commit_output.find("nothing to commit") != std::string::npos) {
            return CommandResult::Ok("Nothing to commit, working tree clean.");
        }
        return CommandResult::Fail("Commit failed: " + commit_output);
    }

    oss << commit_output << "\n";
    oss << "\nSuccessfully committed with message:\n  " << commit_message << "\n";

    return CommandResult::Ok(oss.str());
}

CommandResult CommandRegistry::CmdMemory(const CommandContext& ctx, const std::vector<std::string>& args) {
    std::ostringstream oss;

    // Get memory manager - use workspace from context
    std::filesystem::path workspace;
    if (ctx.workspace.empty()) {
        workspace = std::filesystem::current_path();
    } else {
        workspace = ctx.workspace;
    }
    aicode::MemoryManager mem_mgr(workspace);

    if (args.empty() || args[0] == "list") {
        // List memory entries
        oss << "=== Long-term Memory ===\n\n";

        // List daily memory files
        std::filesystem::path memory_dir = mem_mgr.GetBaseDir() / "memory";
        if (!std::filesystem::exists(memory_dir)) {
            return CommandResult::Ok("No memory entries found. Use /memory add <note> to create one.");
        }

        std::vector<std::filesystem::directory_entry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(memory_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".md") {
                entries.push_back(entry);
            }
        }

        // Sort by date (newest first)
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.path().filename() > b.path().filename();
        });

        if (entries.empty()) {
            return CommandResult::Ok("No memory entries found.");
        }

        oss << "Memory entries (" << entries.size() << "):\n\n";
        for (size_t i = 0; i < std::min(entries.size(), size_t(10)); ++i) {
            const auto& entry = entries[i];
            std::string filename = entry.path().filename().string();
            oss << "  " << (i + 1) << ". " << filename.substr(0, filename.size() - 3) << "\n";
        }

        if (entries.size() > 10) {
            oss << "\n  ... and " << (entries.size() - 10) << " more entries.\n";
        }

        return CommandResult::Ok(oss.str());
    }

    if (args[0] == "add" && args.size() >= 2) {
        // Add memory entry
        std::string content;
        for (size_t i = 1; i < args.size(); ++i) {
            if (i > 1) content += " ";
            content += args[i];
        }

        mem_mgr.SaveDailyMemory(content);
        return CommandResult::Ok("Memory entry saved.");
    }

    if (args[0] == "search" && args.size() >= 2) {
        // Search memory
        std::string query;
        for (size_t i = 1; i < args.size(); ++i) {
            if (i > 1) query += " ";
            query += args[i];
        }

        auto results = mem_mgr.SearchMemory(query);
        if (results.empty()) {
            return CommandResult::Ok("No memory entries found matching: " + query);
        }

        oss << "Search results for '" << query << "' (" << results.size() << "):\n\n";
        for (size_t i = 0; i < std::min(results.size(), size_t(5)); ++i) {
            oss << (i + 1) << ". " << results[i] << "\n\n";
        }

        return CommandResult::Ok(oss.str());
    }

    if (args[0] == "delete" && args.size() >= 2) {
        // Delete memory entry (by date)
        std::string date = args[1];
        std::filesystem::path memory_file = mem_mgr.GetBaseDir() / "memory" / (date + ".md");

        if (std::filesystem::exists(memory_file)) {
            std::filesystem::remove(memory_file);
            return CommandResult::Ok("Memory entry deleted: " + date);
        }
        return CommandResult::Fail("Memory entry not found: " + date);
    }

    return CommandResult::Fail("Unknown memory subcommand. Use: list, add, search, delete");
}

CommandResult CommandRegistry::CmdSummary(const CommandContext& ctx, const std::vector<std::string>& args) {
    std::ostringstream oss;
    bool brief = false;
    bool detailed = false;

    // Parse arguments
    for (const auto& arg : args) {
        if (arg == "--brief") {
            brief = true;
        } else if (arg == "--detailed") {
            detailed = true;
        }
    }

    auto& session_mgr = SessionManager::GetInstance();
    std::string session_id = session_mgr.GetLastSessionId();

    if (session_id.empty()) {
        return CommandResult::Ok("No active session found.");
    }

    // Load session
    std::vector<MessageSchema> messages;
    nlohmann::json metadata;

    if (!session_mgr.LoadSession(session_id, messages, metadata)) {
        return CommandResult::Fail("Failed to load session: " + session_id);
    }

    oss << "=== Session Summary ===\n\n";
    oss << "Session ID: " << session_id << "\n";
    oss << "Created: " << metadata.value("created_at", "unknown") << "\n";

    if (metadata.contains("workspace")) {
        oss << "Workspace: " << metadata["workspace"] << "\n";
    }

    // Count statistics
    int user_messages = 0, assistant_messages = 0, tool_calls = 0;
    std::set<std::string> files_modified;

    for (const auto& msg : messages) {
        if (msg.role == "user") {
            user_messages++;
        } else if (msg.role == "assistant") {
            assistant_messages++;
            for (const auto& content : msg.content) {
                if (content.type == "tool_use") {
                    tool_calls++;
                }
            }
        }
    }

    oss << "\nStatistics:\n";
    oss << "  User messages: " << user_messages << "\n";
    oss << "  Assistant messages: " << assistant_messages << "\n";
    oss << "  Tool calls: " << tool_calls << "\n";

    // Token usage - use global stats
    auto& tracker = TokenTracker::GetInstance();
    auto total_stats = tracker.GetExtendedTotalStats();

    oss << "\nToken Usage:\n";
    oss << "  Input tokens: " << total_stats.prompt_tokens << "\n";
    oss << "  Output tokens: " << total_stats.completion_tokens << "\n";
    oss << "  Total tokens: " << total_stats.total_tokens << "\n";
    oss << "  Cost: $" << std::fixed << std::setprecision(4) << tracker.GetTotalEstimatedCost() << "\n";

    if (detailed && !messages.empty()) {
        oss << "\n=== Recent Messages ===\n\n";
        int start = std::max(0, static_cast<int>(messages.size()) - 10);
        for (size_t i = start; i < messages.size(); ++i) {
            const auto& msg = messages[i];
            oss << "[" << msg.role << "]: ";
            if (!msg.content.empty() && msg.content[0].type == "text") {
                std::string text = msg.content[0].text;
                if (text.size() > 100) {
                    text = text.substr(0, 97) + "...";
                }
                oss << text << "\n";
            } else {
                oss << "(no text content)\n";
            }
        }
    }

    if (brief) {
        oss << "\n[Brief mode: use --detailed for more info]\n";
    }

    return CommandResult::Ok(oss.str());
}

}  // namespace aicode
