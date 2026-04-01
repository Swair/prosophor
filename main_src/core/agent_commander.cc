// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "core/agent_commander.h"

#include <csignal>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>

#include "common/log_wrapper.h"
#include "common/banner.h"
#include "common/input_queue.h"
#include "core/agent_core.h"
#include "managers/memory_manager.h"
#include "core/skill_loader.h"
#include "core/system_prompt.h"
#include "managers/buddy_manager.h"
#include "cli/command_registry.h"
#include "providers/qwen_provider.h"
#include "providers/anthropic_provider.h"
#include "tools/tool_registry.h"
#include <thread>
#include <atomic>

namespace aicode {

namespace {
AgentCommander* g_commander_ptr = nullptr;
}  // namespace

void SignalHandler(int /* signum */) {
    if (g_commander_ptr) {
        g_commander_ptr->Stop();
    }
    std::cout << "\nCtrl+C received, exiting...\n";
    std::exit(0);
}

AgentCommander& AgentCommander::GetInstance() {
    static AgentCommander instance;
    return instance;
}

AgentCommander::AgentCommander()
    : interrupted_(false),
      waiting_permission_(false),
      permission_response_('\0'),
      workspace_path_(aicode::AiCodeConfig::BaseDir() / "workspace") {
    InitializeComponents();
}

AgentCommander::~AgentCommander() {
    if (memory_manager_) {
        memory_manager_->StopFileWatcher();
    }
}

void AgentCommander::InitializeComponents() {
    LOG_INFO("Initializing AiCode components...");

    // Get config from singleton (loads from ~/.aicode/settings.json)
    config_ = aicode::AiCodeConfig::GetInstance();

    // Get agent config from default provider's default agent
    auto provider_it = config_.providers.find(config_.default_provider);
    if (provider_it == config_.providers.end()) {
        LOG_WARN("Provider '{}' not found, using defaults", config_.default_provider);
        provider_it = config_.providers.find("anthropic");
    }
    if (provider_it == config_.providers.end()) {
        LOG_ERROR("No provider configured");
        throw std::runtime_error("No provider configured");
    }

    const auto& provider_config = provider_it->second;
    agent_config_ = provider_config.GetDefaultAgent();

    LOG_INFO("Using provider: {}, agent: {}, model: {}",
             config_.default_provider, agent_config_.name, agent_config_.model);

    // Create workspace directory
    std::filesystem::create_directories(workspace_path_);

    // Create AGENTS.md if not exists
    auto agents_file = workspace_path_ / "AGENTS.md";
    if (!std::filesystem::exists(agents_file)) {
        std::ofstream ofs(agents_file);
        if (ofs) {
            ofs << "# AI Agent Instructions\n\nThis file contains instructions for AI agents.\n";
            ofs.close();
        }
    }

    // Memory Manager
    memory_manager_ = std::make_shared<MemoryManager>(workspace_path_);
    memory_manager_->LoadWorkspaceFiles();
    memory_manager_->StartFileWatcher();

    // Skill Loader
    skill_loader_ = std::make_shared<SkillLoader>();

    // Tool Registry
    tool_registry_ = std::make_shared<ToolRegistry>();
    tool_registry_->RegisterBuiltinTools();
    tool_registry_->SetWorkspace(workspace_path_.string());

    // Set up permission confirmation callback for interactive mode
    auto& perm_manager = PermissionManager::GetInstance();
    perm_manager.SetConfirmCallback(
        [this, &perm_manager](const std::string& tool_name, const nlohmann::json& input, const std::string& reason) -> bool {
            std::string cmd_display;
            if (tool_name == "bash" && input.contains("command")) {
                cmd_display = input["command"].get<std::string>();
            } else if (tool_name == "read_file" && input.contains("path")) {
                cmd_display = "read " + input["path"].get<std::string>();
            } else if (tool_name == "write_file" && input.contains("path")) {
                cmd_display = "write " + input["path"].get<std::string>();
            } else if (tool_name == "edit_file" && input.contains("path")) {
                cmd_display = "edit " + input["path"].get<std::string>();
            }

            // Check if this is a safe read-only command - auto-allow
            bool is_safe_command = false;
            if (tool_name == "bash" && !cmd_display.empty()) {
                // Safe read-only commands
                static const std::vector<std::string> safe_cmds = {
                    "cat ", "ls ", "find ", "grep ", "head ", "tail ",
                    "wc ", "sort ", "uniq ", "pwd ", "echo ", "true",
                    "git status", "git diff ", "git log ", "git show ",
                    "git branch", "git remote", "git rev-parse",
                    "test -", "[ -", "stat ", "file ", "which ", "whereis "
                };
                for (const auto& safe : safe_cmds) {
                    if (cmd_display.find(safe) == 0) {
                        is_safe_command = true;
                        break;
                    }
                }
                // Never auto-allow dangerous commands
                static const std::vector<std::string> unsafe_cmds = {
                    "rm -rf", "rm -r", "rm -f", "sudo", "mkfs", "dd ",
                    "chmod -R", "chown -R", "curl ", "wget ", "bash -c",
                    "sh -c", "eval ", "exec ", ":(){:|:&}"
                };
                for (const auto& unsafe : unsafe_cmds) {
                    if (cmd_display.find(unsafe) != std::string::npos) {
                        is_safe_command = false;
                        break;
                    }
                }
            } else if (tool_name == "read_file") {
                // read_file is safe
                is_safe_command = true;
            }

            if (is_safe_command) {
                // Auto-allow and add to allow rules
                LOG_DEBUG("Auto-allowing safe command: {}", cmd_display);
                PermissionRule rule;
                if (tool_name == "bash") {
                    rule.tool_name = "bash";
                    rule.command_pattern = cmd_display.substr(0, cmd_display.find(' ')) + " *";
                } else {
                    rule.tool_name = tool_name;
                }
                rule.default_level = PermissionLevel::Allow;
                perm_manager.AddAllowRule(rule);
                return true;
            }

            // Show permission prompt
            std::cout << "\n╔════════════════════════════════════════════╗\n";
            std::cout << "║         TOOL PERMISSION REQUEST            ║\n";
            std::cout << "╚════════════════════════════════════════════╝\n\n";
            std::cout << "Tool: " << tool_name;
            if (!cmd_display.empty()) {
                std::cout << "\nCommand: " << cmd_display;
            }
            std::cout << "\nReason: " << reason << "\n\n";
            std::cout << "Allow this operation? (y/n, or 'a' to always allow): " << std::flush;

            // Signal producer to pause reading and wait for permission response
            waiting_permission_ = true;
            permission_response_ = '\0';

            // Wait for user response from producer thread
            std::unique_lock<std::mutex> lock(permission_mutex_);
            permission_cv_.wait(lock, [this]() {
                return permission_response_ != '\0';
            });

            char response = permission_response_.load();
            waiting_permission_ = false;

            bool allowed = (response == 'y' || response == 'Y');

            // If user chose 'always allow', add a rule
            if (response == 'a' || response == 'A') {
                allowed = true;
                PermissionRule rule;
                if (tool_name == "bash" && !cmd_display.empty()) {
                    rule.tool_name = "bash";
                    // Extract base command
                    std::string base_cmd = cmd_display.substr(0, cmd_display.find(' '));
                    rule.command_pattern = base_cmd + " *";
                } else {
                    rule.tool_name = tool_name;
                }
                rule.default_level = PermissionLevel::Allow;
                perm_manager.AddAllowRule(rule);
                LOG_INFO("Added allow rule for: {}", tool_name);
            }

            return allowed;
        }
    );

    // Command Registry (for tab completion)
    command_registry_ = &CommandRegistry::GetInstance();
    command_registry_->Initialize();

    // LLM Provider
    llm_provider_ = std::make_shared<QwenProvider>(
        provider_config.api_key, provider_config.base_url, provider_config.timeout);
    LOG_INFO("Using {} provider with base_url: {}", config_.default_provider, provider_config.base_url);

    // Agent CloseLoop
    agent_core_ = std::make_shared<AgentCore>(
        memory_manager_,
        skill_loader_,
        tool_registry_->GetToolSchemas(),
        [&](const std::string& tool_name, const nlohmann::json& args) -> std::string {
            // Check permission before executing tool
            auto perm_result = perm_manager.CheckPermission(tool_name, args);

            if (perm_result.level == PermissionLevel::Deny) {
                LOG_WARN("Tool {} confirmation denied by user", tool_name);
                throw std::runtime_error("Tool execution denied by user");
            }

            if (perm_result.level == PermissionLevel::Ask) {
                // Request user confirmation
                std::string reason = perm_result.reason;
                if (tool_name == "bash" && args.contains("command")) {
                    std::string cmd = args["command"].get<std::string>();
                    // Check for dangerous commands
                    if (cmd.find("rm -rf") != std::string::npos ||
                        cmd.find("sudo rm") != std::string::npos ||
                        cmd.find("mkfs") != std::string::npos ||
                        cmd.find(":(){:|:&}") != std::string::npos) {
                        reason = "Dangerous command detected";
                    }
                }

                if (!perm_manager.RequestUserConfirmation(tool_name, args, reason)) {
                    LOG_WARN("Tool {} confirmation denied by user", tool_name);
                    throw std::runtime_error("Tool execution denied by user");
                }
            }

            return tool_registry_->ExecuteTool(tool_name, args);
        },
        [&](const ChatRequest& request)-> ChatResponse {
            return llm_provider_->Chat(request);
        },
        [&](const ChatRequest& request, std::function<void(const ChatResponse&)> callback) {
            llm_provider_->ChatStream(request, callback);
        },
        agent_config_
    );

    LOG_INFO("AgentCore initialized: model={}, temp={}, max_tokens={}, context_window={}",
             agent_config_.model, agent_config_.temperature,
             agent_config_.max_tokens, agent_config_.context_window);
}

std::vector<SystemSchema> AgentCommander::BuildSystemPrompt() {
    return aicode::BuildSystemPrompt(config_, skill_loader_, memory_manager_, workspace_path_.string());
}

void AgentCommander::PrintHelp() {
    std::cout << "\nCommands:\n";
    std::cout << "  /help     - Show this help message\n";
    std::cout << "  /clear    - Clear conversation history\n";
    std::cout << "  /model    - Show/change current model\n";
    std::cout << "  /config   - Show current configuration\n";
    std::cout << "  /memory   - Show/save daily memory\n";
    std::cout << "  /skills   - List available skills\n";
    std::cout << "  /<skill>  - Load a skill (e.g., /git, /test)\n";
    std::cout << "  /buddy    - Show your companion pet\n";
    std::cout << "  /buddy hatch - Generate a new companion\n";
    std::cout << "  /permissions - Show permission rules\n";
    std::cout << "  /perms clear - Clear all permission rules\n";
    std::cout << "  /exit     - Exit the application\n";
    std::cout << "\n";
    std::cout << "Permission tips:\n";
    std::cout << "  - Safe commands (cat, ls, find, grep, git status, etc.) are auto-allowed\n";
    std::cout << "  - When prompted, enter 'y' to allow once, 'a' to always allow\n";
    std::cout << "\n";
}

std::string AgentCommander::ReadLine(const std::string& prompt_str) {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    std::cout << "\033[" << w.ws_row << ";1H\033[K" << prompt_str;
    std::cout.flush();

    // Use select() for blocking with timeout, allowing ESC check
    std::string line;
    while (!interrupted_) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        struct timeval tv = {0, 50000};  // 50ms timeout
        int ret = select(STDIN_FILENO + 1, &set, nullptr, nullptr, &tv);

        if (ret > 0 && FD_ISSET(STDIN_FILENO, &set)) {
            char c;
            int n = read(STDIN_FILENO, &c, 1);
            if (n > 0) {
                if (c == 27) {  // ESC key
                    // Just set interrupted_ and return, don't break the loop
                    interrupted_ = true;
                    return line;
                }
                if (c == '\n' || c == '\r') {
                    break;
                }
                if (c == 127 || c == 8) {  // Backspace
                    if (!line.empty()) {
                        line.pop_back();
                        std::cout << "\b \b" << std::flush;
                    }
                } else if ((c >= 32 && c < 127) || (unsigned char)c >= 0x80) {
                    // ASCII or UTF-8 multibyte
                    line += c;
                    std::cout << c << std::flush;
                }
            }
        }
    }

    std::cout << "\n";
    std::cout.flush();
    return line;
}

// Render Biscuit sprite to bottom-right corner (random species each time)
static void PrintBiscuitAtCorner() {
    auto& buddy = BuddyManager::GetInstance();

    // Get terminal size (default to 80x24 if unknown)
    int term_width = 80;
    int term_height = 24;

#ifdef TIOCGWINSZ
    #include <sys/ioctl.h>
    #include <unistd.h>
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        term_width = w.ws_col;
        term_height = w.ws_row;
    }
#endif

    auto sprite = buddy.RenderRandomSprite(0);
    int sprite_height = static_cast<int>(sprite.size());
    int sprite_width = 0;
    for (const auto& s : sprite) {
        if (static_cast<int>(s.size()) > sprite_width) {
            sprite_width = static_cast<int>(s.size());
        }
    }

    // Move cursor to bottom-right corner
    int start_row = term_height - sprite_height - 1;
    int start_col = term_width - sprite_width - 2;

    for (size_t i = 0; i < sprite.size(); i++) {
        // Move to position and print
        std::cout << "\033[" << (start_row + i) << ";" << start_col << "H" << sprite[i];
    }
    // Move cursor back to input line
    std::cout << "\033[" << (start_row + sprite_height + 1) << ";1H";
    std::cout << "❯ ";
    std::cout.flush();
}

bool AgentCommander::HandleCommand(const std::string& line) {
    if (line == "/help") {
        PrintHelp();
        return true;
    }
    if (line == "/exit" || line == "/quit") {
        LOG_INFO("Exiting AiCode...");
        return true;  // Signal to exit
    }
    if (line == "/clear") {
        if (agent_core_) {
            agent_core_->ClearHistory();
        }
        LOG_INFO("Conversation history cleared");
        std::cout << "History cleared\n";
        return true;
    }
    if (line == "/config") {
        std::cout << "Current model: " << agent_config_.model << "\n";
        std::cout << "Temperature: " << agent_config_.temperature << "\n";
        std::cout << "Max tokens: " << agent_config_.max_tokens << "\n";
        std::cout << "Context window: " << agent_config_.context_window << "\n";
        std::cout << "Max iterations: "
                  << agent_core_->GetConfig().DynamicMaxIterations() << "\n";
        return true;
    }
    if (line == "/model") {
        std::cout << "Current model: " << agent_config_.model << "\n";
        size_t space_pos = line.find(' ');
        if (space_pos != std::string::npos) {
            std::string new_model = line.substr(space_pos + 1);
            if (!new_model.empty()) {
                agent_core_->SetModel(new_model);
                std::cout << "Model changed to: " << new_model << "\n";
            }
        }
        return true;
    }
    if (line == "/history") {
        if (agent_core_) {
            for (const auto& msg : agent_core_->GetHistory()) {
                std::cout << msg.role << ": " << msg.text() << "\n";
            }
        }
        return true;
    }
    if (line == "/permissions" || line == "/perms") {
        // Show permission rules
        std::cout << "\n=== Permission Rules ===\n\n";
        auto& pm = PermissionManager::GetInstance();

        auto allow_rules = pm.GetAllowRules();
        if (!allow_rules.empty()) {
            std::cout << "Allow rules (" << allow_rules.size() << "):\n";
            for (const auto& rule : allow_rules) {
                std::cout << "  - tool: " << rule.tool_name;
                if (!rule.command_pattern.empty()) {
                    std::cout << ", command: " << rule.command_pattern;
                }
                if (!rule.path_pattern.empty()) {
                    std::cout << ", path: " << rule.path_pattern;
                }
                std::cout << "\n";
            }
        } else {
            std::cout << "No allow rules.\n";
        }

        auto deny_rules = pm.GetDenyRules();
        if (!deny_rules.empty()) {
            std::cout << "\nDeny rules (" << deny_rules.size() << "):\n";
            for (const auto& rule : deny_rules) {
                std::cout << "  - tool: " << rule.tool_name;
                if (!rule.command_pattern.empty()) {
                    std::cout << ", command: " << rule.command_pattern;
                }
                std::cout << "\n";
            }
        }

        std::cout << "\nMode: " << pm.GetMode() << "\n";
        std::cout << "\nUse '/perms clear' to clear all rules\n";
        std::cout << "\n";
        return true;
    }
    if (line == "/permissions clear" || line == "/perms clear") {
        auto& pm = PermissionManager::GetInstance();
        pm.ClearRules();
        std::cout << "All permission rules cleared\n";
        return true;
    }
    if (line == "/memory") {
        std::ostringstream memory_content;
        if (agent_core_) {
            for (const auto& msg : agent_core_->GetHistory()) {
                memory_content << msg.role << ": " << msg.text() << "\n";
            }
        }
        memory_manager_->SaveDailyMemory(memory_content.str());
        std::cout << "Memory saved\n";
        return true;
    }
    if (line == "/skills list") {
        // List available skills
        std::cout << "\n## Available Skills\n";
        auto skills = skill_loader_->LoadSkillsFromDirectory(workspace_path_ / "skills");
        if (skills.empty()) {
            std::cout << "No skills found in workspace/skills directory.\n";
        } else {
            std::cout << "Found " << skills.size() << " skills:\n\n";
            for (const auto& skill : skills) {
                std::cout << "  /" << skill.name;
                if (!skill.description.empty()) {
                    std::cout << " - " << skill.description;
                }
                std::cout << "\n";
            }
        }
        std::cout << "\n";
        return true;
    }

    // Handle /buddy command (before generic skill handling)
    if (line == "/buddy" || line.find("/buddy ") == 0) {
        auto& buddy = BuddyManager::GetInstance();

        if (line == "/buddy") {
            // Show current companion
            if (!buddy.HasCompanion()) {
                std::cout << "\nNo companion found. Hatch one with /buddy hatch\n\n";
            } else {
                // Render ASCII art
                std::cout << "\n";
                auto sprite = buddy.RenderSprite(0);
                for (const auto& line : sprite) {
                    std::cout << line << "\n";
                }
                std::cout << "\n" << buddy.RenderInfo() << "\n\n";
            }
            return true;
        }

        if (line == "/buddy hatch") {
            // Generate new companion
            std::string user_id = "default_user";  // TODO: Get from config
            auto companion = buddy.GenerateCompanion(user_id);

            std::cout << "\n🎉 A new companion has hatched!\n\n";
            auto sprite = buddy.RenderSprite(0);
            for (const auto& line : sprite) {
                std::cout << line << "\n";
            }
            std::cout << "\n" << buddy.RenderInfo() << "\n\n";
            std::cout << "Your companion needs a name! Use /buddy name <name> to name it.\n\n";
            return true;
        }

        if (line.find("/buddy name ") == 0) {
            std::string name = line.substr(12);
            if (!name.empty()) {
                buddy.SetCompanionName(name);
                std::cout << "Companion named: " << name << "\n\n";
            }
            return true;
        }

        if (line == "/buddy save") {
            if (buddy.SaveCompanion()) {
                std::cout << "Companion saved!\n\n";
            } else {
                std::cout << "Failed to save companion.\n\n";
            }
            return true;
        }

        std::cout << "Unknown /buddy subcommand. Use /buddy hatch, /buddy name <name>, or /buddy save.\n\n";
        return true;
    }

    // Handle /<skillname> format to load a skill
    if (line.size() > 1 && line[0] == '/') {
        std::string skill_name = line.substr(1);
        // Check if it looks like a skill name (alphanumeric, no spaces)
        bool valid_name = true;
        for (char c : skill_name) {
            if (!std::isalnum(c) && c != '_' && c != '-') {
                valid_name = false;
                break;
            }
        }
        if (valid_name) {
            // Try to load the skill
            auto skills = skill_loader_->LoadSkillsFromDirectory(workspace_path_ / "skills");
            bool found = false;
            for (const auto& skill : skills) {
                if (skill.name == skill_name) {
                    found = true;
                    std::cout << "\nLoading skill: " << skill.name << "\n";
                    if (!skill.description.empty()) {
                        std::cout << "Description: " << skill.description << "\n";
                    }
                    // Add skill to config
                    config_.skills.entries[skill_name].enabled = true;
                    // Rebuild system prompt with new skill and update agent
                    system_prompt_ = BuildSystemPrompt();
                    agent_core_->SetSystemPrompt(system_prompt_, true);
                    std::cout << "Skill loaded successfully!\n\n";
                    break;
                }
            }
            if (!found) {
                std::cout << "Skill '" << skill_name << "' not found.\n";
                std::cout << "Use /skills to see available skills.\n\n";
            }
            return true;
        }
    }

    return false;  // Not a command
}

void AgentCommander::ProcessUserMessage(const std::string& line) {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    try {
        LOG_INFO("User: {}", line);

        if (agent_core_) {
            // Reset interrupted_ flag before processing new message
            interrupted_ = false;

            auto response = agent_core_->CloseLoop(line);

            // Check if interrupted during processing
            if (interrupted_) {
                interrupted_ = false;  // Reset for next use
                return;
            }

            // Move cursor up one line (to output area) and print response
            std::cout << "\033[" << (w.ws_row - 1) << ";1H\033[J";

            // Print response
            for (const auto& msg : response) {
                // Check for interrupt during output
                if (interrupted_) {
                    return;
                }

                if (msg.role == "assistant") {
                    std::string text = msg.text();

                    // Also print tool use information
                    for (const auto& block : msg.content) {
                        if (block.type == "tool_use") {
                            if (!text.empty()) text += "\n";
                            text += "[Using tool: " + block.name + "]";
                        } else if (block.type == "tool_result") {
                            if (!text.empty()) text += "\n";
                            text += "[Tool result: " + block.content.substr(0, 100) +
                                    (block.content.size() > 100 ? "..." : "") + "]";
                        }
                    }

                    if (!text.empty()) {
                        std::cout << "AiCode: " << text << std::endl;
                    } else if (!msg.content.empty()) {
                        // MessageSchema has content but no text (e.g., only tool calls)
                        std::cout << "AiCode: [Processing with tools]" << std::endl;
                    }
                }
            }
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Error: {}", e.what());
        // Move cursor to output area for error
        std::cout << "\033[" << (w.ws_row - 1) << ";1H\033[J";
        std::cout << "Error: " << e.what() << std::endl;
    }

    // Redraw input prompt after output
    std::cout << "\033[" << w.ws_row << ";1H\033[K❯ ";
    std::cout.flush();
}

int AgentCommander::Run() {
    // Setup signal handlers
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // Set global pointer for signal handling
    g_commander_ptr = this;

    // Load companion pet (if exists)
    auto& buddy = BuddyManager::GetInstance();
    buddy.LoadCompanion();  // Uses AI_CODE_CONFIG env or ~/.aicode/config.json

    // Print banner (with buddy if enabled in config)
    bool show_buddy = config_.show_buddy;
    aicode::PrintBanner(AICODE_VERSION, show_buddy);

    system_prompt_ = BuildSystemPrompt();

    // Set initial system prompt on agent
    agent_core_->SetSystemPrompt(system_prompt_, true);

    // Input queue for producer-consumer pattern
    InputQueue input_queue;

    // Producer thread: read input and handle commands
    std::thread producer_thread([this, &input_queue, show_buddy]() {
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

        // Save and set terminal mode for the entire thread lifetime
        struct termios orig_termios;
        if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
            struct termios raw = orig_termios;
            raw.c_lflag &= ~(ECHO | ICANON);
            raw.c_iflag &= ~(IXON);
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;
            tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        }

        while (!interrupted_) {
            // Show prompt or read silently during permission prompt
            if (waiting_permission_) {
                // No prompt during permission confirmation
            } else if (show_buddy) {
                PrintBiscuitAtCorner();
            } else {
                std::cout << "❯ ";
            }
            std::cout.flush();

            std::string line = ReadLine();

            // Handle permission response during permission prompt
            if (waiting_permission_) {
                if (!line.empty()) {
                    char c = line[0];
                    if (c == 'y' || c == 'Y' || c == 'n' || c == 'N' || c == 'a' || c == 'A') {
                        // Echo the response
                        std::cout << c << "\n";
                        permission_response_ = c;
                        permission_cv_.notify_one();
                    }
                }
                continue;
            }

            // Check if interrupted during ReadLine (ESC key)
            if (interrupted_) {
                // ESC only cancels current AgentCore execution, not the whole program
                // The interrupted flag will be reset after the current turn
                std::cout << "\033[" << w.ws_row << ";1H\033[K[Interrupted]\n\n";
                // Reset interrupted_ so user can continue typing
                interrupted_ = false;
                agent_core_->Stop();  // Stop the current LLM call
                continue;  // Go back to reading input
            }

            // Normal input handling - skip empty lines
            if (line.empty()) {
                continue;
            }

            if (line[0] == '/') {
                if (HandleCommand(line)) {
                    if (line == "/exit" || line == "/quit" || line == "/bye") {
                        interrupted_ = true;
                        input_queue.NotifyAll();
                        break;
                    }
                    continue;
                }

                std::cout << "Unknown command: " << line << "\n";
                std::cout << "Type /help for available commands\n";
                continue;
            }

            // Pass non-command input to consumer
            input_queue.Push(line);
        }

        // Restore terminal mode
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    });

    // Consumer thread: process user messages (LLM calls happen here)
    while (true) {
        std::string combined = input_queue.WaitAndPopAll(interrupted_);
        if (combined.empty()) {
            break;
        }
        ProcessUserMessage(combined);
    }

    if (interrupted_) {
        std::cout << "\n[Interrupted]\n\n";
    }

    if (producer_thread.joinable()) {
        producer_thread.join();
    }

    LOG_INFO("Shutting down AiCode...");
    return 0;
}

void AgentCommander::Stop() {
    interrupted_ = true;
    if (agent_core_) {
        agent_core_->Stop();
    }
}

}  // namespace aicode
