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
#include "core/messages_schema.h"
#include "core/agent_state.h"  // For AgentState and SystemSchema

namespace aicode {

// Forward declarations
class MemoryManager;
class SkillLoader;
class ToolRegistry;
class AgentCore;
class CommandRegistry;
class LLMProvider;

/// AgentCommander: orchestrates user interaction, command handling, and agent execution
/// Singleton pattern - use AgentCommander::GetInstance() to access
class AgentCommander : public Noncopyable {
 public:
    static AgentCommander& GetInstance();

    /// Run the main interaction loop
    int Run();

    /// Stop the commander
    void Stop();

    /// Check if interrupted (for ESC listener)
    bool IsInterrupted() const { return interrupted_; }

    /// Get current config
    const AiCodeConfig& GetConfig() const { return config_; }

    /// Handle slash commands, return true if command was handled
    bool HandleCommand(const std::string& line);

    /// Process user message and print response
    void ProcessUserMessage(const std::string& line);

    /// Switch to a different provider and agent
    void SwitchProvider(const std::string& provider_name, const std::string& agent_name);

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
    std::shared_ptr<SkillLoader> skill_loader_;
    std::shared_ptr<ToolRegistry> tool_registry_;
    std::shared_ptr<LLMProvider> llm_provider_;
    CommandRegistry* command_registry_ = nullptr;  // Non-owned pointer

    // State
    std::atomic<bool> interrupted_;
    std::atomic<bool> waiting_permission_;  // Flag for permission prompt
    std::atomic<char> permission_response_;  // User response: 'y', 'n', 'a'
    std::mutex permission_mutex_;
    std::condition_variable permission_cv_;
    std::filesystem::path workspace_path_;
    AgentState agent_state_;  // Current session state (messages, config, provider, system_prompt)
};

}  // namespace aicode
