// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include "core/agent_session.h"
#include "providers/llm_provider.h"

namespace aicode {

/// LLM chat callback (non-streaming)
using LlmChatCallback = std::function<ChatResponse(const ChatRequest&)>;

/// LLM streaming callback
using LlmStreamCallback = std::function<void(const ChatRequest&, std::function<void(const ChatResponse&)>)>;

/// Subagent status
enum class SubagentStatus {
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled
};

/// Subagent definition
struct Subagent {
    std::string id;
    std::string name;
    std::string description;
    std::string task;
    SubagentStatus status = SubagentStatus::Pending;
    std::string result;
    std::string error;
    std::string created_at;
    std::string started_at;
    std::string completed_at;
    nlohmann::json context;  // Shared context with parent
    std::vector<MessageSchema> messages;  // Subagent conversation

    std::string StatusToString() const {
        switch (status) {
            case SubagentStatus::Pending: return "pending";
            case SubagentStatus::Running: return "running";
            case SubagentStatus::Completed: return "completed";
            case SubagentStatus::Failed: return "failed";
            case SubagentStatus::Cancelled: return "cancelled";
        }
        return "unknown";
    }
};

/// Subagent coordinator for managing multiple agents
class SubagentCoordinator {
public:
    static SubagentCoordinator& GetInstance();

    /// Initialize coordinator
    void Initialize(LlmChatCallback chat_cb, LlmStreamCallback stream_chat_cb);

    /// Create a subagent
    std::string CreateSubagent(const std::string& name,
                                const std::string& description,
                                const std::string& task);

    /// Start a subagent
    bool StartSubagent(const std::string& agent_id);

    /// Start a subagent (async)
    void StartSubagentAsync(const std::string& agent_id);

    /// Get subagent status
    const Subagent* GetSubagent(const std::string& agent_id) const;

    /// Get all subagents
    std::vector<Subagent> GetAllSubagents() const;

    /// Cancel a subagent
    bool CancelSubagent(const std::string& agent_id);

    /// Wait for subagent to complete
    bool WaitForSubagent(const std::string& agent_id, int timeout_seconds = 300);

    /// Get subagent result
    std::string GetSubagentResult(const std::string& agent_id) const;

    /// Delete a subagent
    bool DeleteSubagent(const std::string& agent_id);

    /// Clear all completed subagents
    void ClearCompleted();

    /// Set system prompt for subagents
    void SetSubagentSystemPrompt(const std::string& prompt) { subagent_system_prompt_ = prompt; }

    /// Get shared context
    nlohmann::json& GetSharedContext() { return shared_context_; }

    /// Send message to subagent
    bool SendMessageToSubagent(const std::string& agent_id, const std::string& message);

    /// Get subagent messages
    const std::vector<MessageSchema>& GetSubagentMessages(const std::string& agent_id) const;

    /// Get running subagent count
    int GetRunningCount() const;

    /// Get pending subagent count
    int GetPendingCount() const;

    /// Launch a subagent directly (simplified API for AgentTool)
    std::string LaunchSubagent(const std::string& prompt,
                               const std::string& subagent_type,
                               const std::string& model);

private:
    SubagentCoordinator() = default;
    ~SubagentCoordinator() = default;

    std::unordered_map<std::string, Subagent> subagents_;
    std::unordered_map<std::string, std::thread> running_threads_;
    LlmChatCallback chat_callback_;
    LlmStreamCallback stream_chat_callback_;
    std::string subagent_system_prompt_ = "You are a specialized subagent working on a specific task.";
    nlohmann::json shared_context_;

    /// Generate unique agent ID
    std::string GenerateAgentId();

    /// Run subagent (internal)
    void RunSubagent(Subagent& agent);
};

}  // namespace aicode
