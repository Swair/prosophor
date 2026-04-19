// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/noncopyable.h"
#include "common/config.h"
#include "common/messages_schema.h"
#include "common/input_event.h"

namespace aicode {

// Forward declarations
class MemoryManager;
class SkillLoader;
class ToolRegistry;
class AgentCore;
class CommandRegistry;
class AgentSessionManager;
class ProviderRouter;
class InputManager;
class OutputManager;
class UIRenderer;
class AgentStateVisualizer;

/// Run mode for AgentCommander
enum class RunMode {
    Terminal,  // Terminal-based I/O
    SDL        // SDL UI-based I/O
};

/// AgentCommander: orchestrates user interaction, command handling, and agent execution
/// Singleton pattern - use AgentCommander::GetInstance() to access
class AgentCommander : public Noncopyable {
 public:
    static AgentCommander& GetInstance();

    /// Set run mode before calling Run()
    void SetMode(RunMode mode) { mode_ = mode; }
    RunMode GetMode() const { return mode_; }

    /// Run the main interaction loop
    int Run();

    /// Stop the commander
    void Stop();

    /// Check if interrupted (for ESC listener)
    bool IsInterrupted() const { return interrupted_; }

    /// Get current config
    const AiCodeConfig& GetConfig() const { return config_; }

    /// Handle input event (called by InputManager)
    void HandleInputEvent(const InputEvent& event);

    /// Handle slash commands, return true if command was handled
    bool HandleCommand(const std::string& line);

    /// Process user message and print response
    void ProcessUserMessage(const std::string& line);

    /// Switch to a different role
    void SwitchRole(const std::string& role_id);

    /// List all available roles
    void ListRoles();

    /// List all active sessions
    void ListSessions();

 private:
    AgentCommander();
    ~AgentCommander();

    /// Initialize all components
    void InitializeComponents();

    /// Build system prompt from skills and identity files
    std::vector<SystemSchema> BuildSystemPrompt();

    /// Print help message
    void PrintHelp();

    /// Get command registry for tab completion
    CommandRegistry* GetCommandRegistry() const { return command_registry_; }

    /// Configuration
    AiCodeConfig config_;
    AgentConfig agent_config_;  // Current agent configuration from default provider

    // Components
    std::shared_ptr<MemoryManager> memory_manager_;
    ToolRegistry* tool_registry_ = nullptr;  // Tool registry (singleton)
    AgentSessionManager* session_manager_ = nullptr;  // Multi-agent session manager (singleton)
    ProviderRouter* provider_router_ = nullptr;      // Provider routing (singleton)
    CommandRegistry* command_registry_ = nullptr;  // Command registry reference
    InputManager* input_manager_ = nullptr;        // Input manager (ownership depends on mode)
    OutputManager* output_manager_ = nullptr;      // Output manager (singleton)

    // State
    std::atomic<bool> interrupted_;
    std::atomic<bool> waiting_permission_;  // Flag for permission prompt
    std::atomic<char> permission_response_;  // User response: 'y', 'n', 'a'
    std::mutex permission_mutex_;
    std::condition_variable permission_cv_;
    std::filesystem::path workspace_path_;

    // Current session
    std::string current_session_id_;  // Current active session ID

    // Run mode
    RunMode mode_ = RunMode::Terminal;
};

}  // namespace aicode
