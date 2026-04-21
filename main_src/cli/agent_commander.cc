// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "cli/agent_commander.h"

#include <csignal>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/log_wrapper.h"
#include "common/banner.h"
#include "common/input_queue.h"
#include "common/string_utils.h"
#include "core/agent_core.h"
#include "managers/memory_manager.h"
#include "managers/buddy_manager.h"
#include "managers/agent_session_manager.h"
#include "managers/agent_role_loader.h"
#include "cli/command_registry.h"
#include "cli/input_manager.h"
#include "cli/output_manager.h"
#include "tools/tool_registry.h"
#include "providers/provider_router.h"
#include "scene/ui_renderer.h"
#include "scene/sdl_app.h"
#include "scene/agent_state_observer.h"
#include "services/lsp_manager.h"

namespace aicode {

namespace {
AgentCommander* g_commander_ptr = nullptr;
}  // namespace

AgentCommander& AgentCommander::GetInstance() {
    static AgentCommander instance;
    return instance;
}

AgentCommander::AgentCommander()
    : interrupted_(false),
      waiting_permission_(false),
      permission_response_('\0'),
      workspace_path_(std::filesystem::current_path()) {
    InitializeComponents();
}

AgentCommander::~AgentCommander() {
    if (memory_manager_) {
        memory_manager_->StopFileWatcher();
    }
    // Don't delete input_manager_ - it's owned by the mode-specific code
}

void AgentCommander::InitializeComponents() {
    LOG_INFO("Initializing AiCode components...");

    // Get config from singleton (loads from ~/.aicode/settings.json)
    config_ = aicode::AiCodeConfig::GetInstance();

    // Create workspace directory (.aicode/workspace under current directory)
    std::filesystem::create_directories(workspace_path_);

    // Create AGENTS.md if not exists
    auto agents_file = workspace_path_ / "AGENTS.md";
    if (!std::filesystem::exists(agents_file)) {
        std::ofstream ofs(agents_file);
        if (ofs) {
            ofs << "# AI Agent Instructions\n\nThis file contains instructions for AI agents.\n";
            ofs.close();
            LOG_INFO("Created AGENTS.md in workspace: {}", agents_file.string());
        }
    }

    // Memory Manager
    memory_manager_ = std::make_shared<MemoryManager>(workspace_path_);
    memory_manager_->LoadWorkspaceFiles();
    memory_manager_->StartFileWatcher();

    // Tool Registry - use singleton to ensure consistency with AgentRoleLoader
    tool_registry_ = &ToolRegistry::GetInstance();
    tool_registry_->SetWorkspace(workspace_path_.string());

    // Set permission confirmation callback
    tool_registry_->SetPermissionConfirmCallback(
        [](const std::string& tool_name, const nlohmann::json& input, const std::string& reason) -> bool {
            std::string command_desc;
            if (tool_name == "bash" && input.contains("command")) {
                command_desc = input["command"].get<std::string>();
                if (command_desc.length() > 60) {
                    command_desc = command_desc.substr(0, 57) + "...";
                }
            }

            std::cout << "\n[Permission Required] Tool: " << tool_name;
            if (!command_desc.empty()) {
                std::cout << "  Command: " << command_desc;
            }
            std::cout << "  Reason: " << reason;
            std::cout << "\n  Allow this action? [Y/N or y/n]: " << std::flush;

            std::string response;
#ifdef _WIN32
            // Thread-safe console input - uses Windows API, doesn't affect stdin
            HANDLE hConsole = GetStdHandle(STD_INPUT_HANDLE);
            char buf[256] = {};
            DWORD charsRead = 0;
            ReadConsoleA(hConsole, buf, sizeof(buf) - 1, &charsRead, nullptr);
            // Remove trailing \r\n
            while (charsRead > 0 && (buf[charsRead-1] == '\r' || buf[charsRead-1] == '\n')) {
                charsRead--;
            }
            response = std::string(buf, charsRead);
#else
            std::getline(std::cin, response);
#endif
            return response == "y" || response == "Y";
        });

    // Session Manager (Multi-agent) - use singleton
    session_manager_ = &AgentSessionManager::GetInstance();

    // Tool executor callback - ToolRegistry handles permission checks internally
    ToolExecutorCallback tool_executor =
        [this](const std::string& tool_name, const nlohmann::json& args) -> std::string {
            return tool_registry_->ExecuteTool(tool_name, args);
        };

    // Initialize Session Manager
    session_manager_->Initialize(memory_manager_, tool_executor);

    // Set output callback to notify UI based on run mode
    session_manager_->SetOutputCallback(
        [this](const std::string& session_id, const std::string& role_id,
               AgentRuntimeState state, const std::string& state_msg,
               const std::optional<MessageSchema>& reply) {
            // LOG_DEBUG("[Callback] mode_={}, state={}", mode_ == RunMode::Terminal ? "Terminal" : "SDL", static_cast<int>(state));
            // Notify state visualizer (capybara UI)
            AgentStateVisualizer::GetInstance().SetAgentState(state, state_msg);
            // Notify message UI based on run mode

            // Terminal mode: output via OutputManager
            if (state == AgentRuntimeState::THINKING) {
                // std::cout << session_id << " - " << role_id << ": " << state_msg << std::endl;
            }
            else if(state == AgentRuntimeState::STREAM_MODE_START) {
                std::cout << "< " << std::flush;
            }
            else if(state == AgentRuntimeState::STREAM_TYPING) {
                std::cout << reply->text() << std::flush;
            }
            else if(state == AgentRuntimeState::STREAM_MODE_COMPLETE) {
                std::cout << "\n> " << std::flush;
            }
            else if(state == AgentRuntimeState::COMPLETE) {
                std::cout << "< " << reply->text() << std::endl;
            }
            else if(state == AgentRuntimeState::STATE_ERROR) {
                LOG_ERROR("Error: {}", state_msg);
                if (reply) {
                    LOG_ERROR("Details: {}", reply->text());
                }
            }
            else if(state == AgentRuntimeState::EXECUTING_TOOL) {
                LOG_DEBUG("Executing tool: {}", state_msg);
            }

            if (mode_ == RunMode::SDL) {
                // SDL mode: add message to chat history
                if (state == AgentRuntimeState::THINKING) {
                    // THINKING 状态：创建空的 Agent 消息占位（流式响应的容器）
                    UIRenderer::Instance().StartAssistantMessage();
                }
                else if (state == AgentRuntimeState::STREAM_TYPING) {
                    // 流式输入：追加内容到 Agent 消息
                    UIRenderer::Instance().UpdateLastMessage(reply->text());
                }
            }
        });

    // Provider Router (singleton)
    provider_router_ = &ProviderRouter::GetInstance();
    provider_router_->Initialize(config_);

    // LSP Manager (optional code intelligence)
    auto& lsp_manager = aicode::LspManager::GetInstance();
    lsp_manager.Initialize();
    LOG_INFO("LSP integration initialized with {} servers",
             lsp_manager.GetRegisteredServers().size());

    // Command Registry (for tab completion)
    command_registry_ = &CommandRegistry::GetInstance();
    command_registry_->Initialize();

    // Output Manager
    output_manager_ = &OutputManager::GetInstance();

    // Create default session using default_role config
    std::string default_role_id = config_.default_role;

    // Verify default_role exists, fallback to first available role if not
    if (!session_manager_->GetRole(default_role_id)) {
        LOG_WARN("Default role '{}' not found, using first available role", default_role_id);
        auto roles = session_manager_->ListRoles();
        if (!roles.empty()) {
            default_role_id = roles[0];
        } else {
            LOG_ERROR("No roles available, using hardcoded 'default'");
            default_role_id = "default";
        }
    }

    current_session_id_ = session_manager_->CreateSession(default_role_id, "Default session");

    LOG_INFO("InitializeComponents finished, current_session: {} (role: {})",
             current_session_id_, default_role_id);
}

void AgentCommander::SwitchRole(const std::string& role_id) {
    const AgentRole* role = session_manager_->GetRole(role_id);
    if (!role) {
        std::cout << "Role not found: " << role_id << std::endl;
        std::cout << "Available roles: ";
        for (const auto& r : session_manager_->ListRoles()) {
            std::cout << r << " ";
        }
        std::cout << std::endl;
        return;
    }

    // Create new session with this role
    current_session_id_ = session_manager_->CreateSession(role_id, "Switched to " + role->name);
    LOG_INFO("Switched to role: {} (session: {})", role_id, current_session_id_);
    std::cout << "Switched to role: " << role->name << " (" << role_id << ")" << std::endl;
}

void AgentCommander::ListRoles() {
    std::cout << "\nAvailable roles:\n";
    for (const auto& role_id : session_manager_->ListRoles()) {
        const AgentRole* role = session_manager_->GetRole(role_id);
        if (role) {
            std::cout << "  " << role_id << " - " << role->name << " (" << role->description << ")\n";
        }
    }
    std::cout << std::endl;
}

void AgentCommander::ListSessions() {
    std::cout << "\nActive sessions:\n";
    for (const auto& session_id : session_manager_->ListSessions()) {
        const AgentSession* session = session_manager_->GetSession(session_id);
        if (session) {
            std::string current_marker = (session_id == current_session_id_) ? " [CURRENT]" : "";
            std::cout << "  " << session_id << " - " << session->task_description << current_marker << "\n";
        }
    }
    std::cout << std::endl;
}

void AgentCommander::PrintHelp() {
    std::cout << "\nCommands:\n";

    // Get command descriptions from CommandRegistry
    auto& registry = CommandRegistry::GetInstance();
    auto descriptions = registry.GetCommandDescriptions();

    // Calculate max name length for padding
    size_t max_name_len = 0;
    for (const auto& [name, desc] : descriptions) {
        max_name_len = std::max(max_name_len, name.length());
    }

    // Print commands with aligned descriptions
    for (const auto& [name, desc] : descriptions) {
        std::cout << "  /" << name;
        size_t padding = max_name_len - name.length();
        if (padding > 0) {
            std::cout << std::string(padding, ' ');
        }
        std::cout << " - " << desc << "\n";
    }

    std::cout << "\n";
}

bool AgentCommander::HandleCommand(const std::string& line) {
    if (line.empty() || line[0] != '/') {
        return false;  // Not a command
    }

    // Parse command line
    std::vector<std::string> args = CommandRegistry::ParseCommandLine(line);
    if (args.empty()) {
        return false;
    }

    std::string cmd_name = args[0].substr(1);  // Remove leading '/'
    std::vector<std::string> cmd_args(args.begin() + 1, args.end());

    // Exit commands - these must be handled here as they affect the main loop
    if (cmd_name == "exit" || cmd_name == "quit" || cmd_name == "bye") {
        LOG_INFO("Exiting AiCode...");
        interrupted_ = true;
        return true;
    }

    // Commands that need AgentCommander context (session_manager_)
    if (cmd_name == "role" && !cmd_args.empty()) {
        const AgentRole* role = session_manager_->GetRole(cmd_args[0]);
        if (!role) {
            std::cout << "Role not found: " << cmd_args[0] << std::endl;
            std::cout << "Available roles: ";
            for (const auto& r : session_manager_->ListRoles()) {
                std::cout << r << " ";
            }
            std::cout << std::endl;
            return true;
        }
        // Create new session with this role
        current_session_id_ = session_manager_->CreateSession(role->id, "Switched to " + role->name);
        LOG_INFO("Switched to role: {} (session: {})", role->id, current_session_id_);
        std::cout << "Switched to role: " << role->name << " (" << role->id << ")" << std::endl;
        return true;
    }

    // All other commands go through CommandRegistry
    CommandContext ctx;
    ctx.workspace = workspace_path_.string();
    ctx.session_id = current_session_id_;
    ctx.user_data = this;
    ctx.agent_session = session_manager_->GetSession(current_session_id_);

    auto result = CommandRegistry::GetInstance().ExecuteCommand(cmd_name, cmd_args, ctx);

    if (result.success) {
        if (!result.output.empty()) {
            std::cout << result.output << std::endl;
        }
        return true;
    } else {
        std::cout << result.error << "\n";
        if (result.error.find("Unknown command") != std::string::npos) {
            std::cout << "Type /help for available commands.\n";
        }
        return true;
    }
}

void AgentCommander::HandleInputEvent(const InputEvent& event) {
    if (event.IsInterrupt()) {
        LOG_INFO("Interrupt received");
        interrupted_ = true;
        // Stop current session - set its stop_requested flag
        if (!current_session_id_.empty()) {
            auto* session = session_manager_->GetSession(current_session_id_);
            if (session) {
                session->stop_requested = true;
            }
        }
        return;
    }

    if (event.IsCommand()) {
        std::string line = event.GetCommandText();
        LOG_DEBUG("Command: {}", line);
        HandleCommand(line);
    } else if (event.IsText()) {
        std::string text = event.GetText();
        LOG_DEBUG("User text: {}", text);
        ProcessUserMessage(text);
    }
}

void AgentCommander::ProcessUserMessage(const std::string& line) {
    try {
#ifdef _WIN32
        // Windows: Raw mode 不回显，需要手动输出
        std::cout << "> " << line << std::endl;
#endif
        // Linux: Raw mode 已通过 RefreshLine 回显，不需要再次输出

        // Reset interrupted_ flag before processing new message
        interrupted_ = false;

        // Send to current session via AgentSessionManager
        // Response will be output via session.output_callback
        session_manager_->SendToSessionAsync(current_session_id_, line);

        // Check if interrupted during processing
        if (interrupted_) {
            interrupted_ = false;
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Error: {}", e.what());
        output_manager_->ShowError(e.what());
    }
}

int AgentCommander::Run() {
    g_commander_ptr = this;

    // Load companion pet (if exists)
    auto& buddy = BuddyManager::GetInstance();
    buddy.LoadCompanion();

    // Print banner (terminal mode only)
    if (mode_ == RunMode::Terminal) {
        bool show_buddy = config_.show_buddy;
        aicode::PrintBanner(AICODE_VERSION, show_buddy);
    }

    // Set output manager mode
    output_manager_->SetMode(mode_ == RunMode::Terminal ? OutputManager::Mode::Terminal : OutputManager::Mode::SDL);

    LOG_INFO("Starting main loop with mode: {}", mode_ == RunMode::Terminal ? "Terminal" : "SDL");

    if (mode_ == RunMode::Terminal) {
        // Create input manager for terminal mode
        std::unique_ptr<InputManager> input_manager = CreateTerminalInput();

        // Set callback
        input_manager->SetCallback([this](const InputEvent& event) {
            HandleInputEvent(event);
        });

        // Store pointer for access during run
        input_manager_ = input_manager.get();

        // Start input loop
        input_manager->Start();

        // Wait for input loop to end
        while (!interrupted_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        input_manager->Stop();
        input_manager_ = nullptr;
    } else {
#ifdef AICODE_SDL_UI
        // SDL mode: input is handled by SdlApp through MediaCore event loop
        // Register input callback with SdlApp
        aicode::SdlApp::GetInstance().SetInputCallback([this](const InputEvent& event) {
            HandleInputEvent(event);
        });
#else
        LOG_ERROR("SDL mode is not available. Please enable AICODE_SDL_UI option.");
        return 1;
#endif
    }

    return interrupted_ ? 0 : 1;
}

void AgentCommander::Stop() {
    interrupted_ = true;
    if (input_manager_) {
        input_manager_->Stop();
    }
}

}  // namespace aicode
