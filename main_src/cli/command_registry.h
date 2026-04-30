// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include "common/noncopyable.h"
#include "core/agent_session.h"

namespace prosophor {

class AgentCore;  // Forward declaration

/// Command execution context
struct CommandContext {
    std::string workspace;
    std::string session_id;
    void* user_data = nullptr;  // For custom context data
    class AgentCore* agent_core = nullptr;  // For accessing agent at runtime
    class AgentSession* agent_session = nullptr;  // For accessing current session state
};

/// Command result
struct CommandResult {
    bool success = true;
    std::string output;
    std::string error;
    int exit_code = 0;

    static CommandResult Ok(const std::string& output = "") {
        return {true, output, "", 0};
    }

    static CommandResult Fail(const std::string& error, int exit_code = 1) {
        return {false, "", error, exit_code};
    }
};

/// Command definition
struct Command {
    std::string name;
    std::string description;
    std::string usage;  // Usage string
    std::vector<std::string> aliases;
    bool requires_args = false;
    std::function<CommandResult(const CommandContext&, const std::vector<std::string>& args)> handler;

    // For tab completion
    std::function<std::vector<std::string>(const std::string& partial)> completer;
};

/// Command registry and executor
class CommandRegistry {
public:
    static CommandRegistry& GetInstance();

    /// Initialize built-in commands
    void Initialize();

    /// Register a command
    void RegisterCommand(const Command& cmd);

    /// Execute a command by name
    CommandResult ExecuteCommand(const std::string& name,
                                  const std::vector<std::string>& args,
                                  const CommandContext& ctx = CommandContext());

    /// Check if command exists
    bool HasCommand(const std::string& name) const;

    /// Get command by name
    const Command* GetCommand(const std::string& name) const;

    /// Get all command names
    std::vector<std::string> GetCommandNames() const;

    /// Get command descriptions
    std::vector<std::pair<std::string, std::string>> GetCommandDescriptions() const;

    /// Tab completion for commands
    std::vector<std::string> CompleteCommand(const std::string& partial) const;

    /// Tab completion for role names
    std::vector<std::string> CompleteRole(const std::string& partial) const;

    /// Tab completion for command arguments
    std::vector<std::string> CompleteArguments(const std::string& cmd_name,
                                                const std::string& partial_arg) const;

    /// Comprehensive tab completion for full command input
    /// Handles both command names and argument completion
    std::vector<std::string> CompleteInput(const std::string& input,
                                            size_t cursor_pos) const;

    /// Parse a command line (handles quotes, escapes)
    static std::vector<std::string> ParseCommandLine(const std::string& line);

    /// Format command help
    std::string GetHelpText(const std::string& cmd_name) const;

    /// Get all commands help
    std::string GetAllCommandsHelp() const;

private:
    CommandRegistry() = default;
    ~CommandRegistry() = default;

    std::unordered_map<std::string, Command> commands_;

    // Built-in command handlers
    CommandResult CmdHelp(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdCost(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdStatus(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdDiff(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdCommit(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdTasks(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdConfig(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdClear(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdMcp(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdResume(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdSessions(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdCompact(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdPlugins(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdSkills(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdSchedule(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdContext(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdDoctor(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdEffort(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdPlan(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdAutoCommit(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdMemory(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdSummary(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdRole(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdRoles(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdModel(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdPermissions(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdHistory(const CommandContext&, const std::vector<std::string>& args);
    CommandResult CmdBye(const CommandContext&, const std::vector<std::string>& args);
};

}  // namespace prosophor
