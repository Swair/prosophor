// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "agents/subagent_coordinator.h"

#include <sstream>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "common/log_wrapper.h"
#include "common/constants.h"
#include "common/time_wrapper.h"
#include "managers/memory_manager.h"
#include "managers/skill_loader.h"
#include "core/agent_session.h"
#include "core/agent_core.h"

namespace prosophor {

SubagentCoordinator& SubagentCoordinator::GetInstance() {
    static SubagentCoordinator instance;
    return instance;
}

void SubagentCoordinator::Initialize(LlmChatCallback chat_cb, LlmStreamCallback stream_chat_cb) {
    chat_callback_ = chat_cb;
    stream_chat_callback_ = stream_chat_cb;
    LOG_DEBUG("SubagentCoordinator initialized");
}

std::string SubagentCoordinator::GenerateAgentId() {
    return SystemClock::GenerateIdWithTimestamp("agent_");
}

std::string SubagentCoordinator::CreateSubagent(const std::string& name,
                                                  const std::string& description,
                                                  const std::string& task) {
    Subagent agent;
    agent.id = GenerateAgentId();
    agent.name = name;
    agent.description = description;
    agent.task = task;
    agent.status = SubagentStatus::Pending;
    agent.created_at = SystemClock::GetCurrentTimestamp();
    agent.context = shared_context_;

    subagents_[agent.id] = agent;
    LOG_INFO("Created subagent: {} - {}", agent.id, name);
    return agent.id;
}

bool SubagentCoordinator::StartSubagent(const std::string& agent_id) {
    auto it = subagents_.find(agent_id);
    if (it == subagents_.end()) {
        LOG_ERROR("Subagent not found: {}", agent_id);
        return false;
    }

    if (it->second.status != SubagentStatus::Pending) {
        LOG_WARN("Subagent {} is not pending (status: {})", agent_id, it->second.StatusToString());
        return false;
    }

    it->second.status = SubagentStatus::Running;
    it->second.started_at = SystemClock::GetCurrentTimestamp();

    // Run subagent synchronously
    RunSubagent(it->second);

    return true;
}

void SubagentCoordinator::StartSubagentAsync(const std::string& agent_id) {
    auto it = subagents_.find(agent_id);
    if (it == subagents_.end()) {
        return;
    }

    if (it->second.status != SubagentStatus::Pending) {
        return;
    }

    it->second.status = SubagentStatus::Running;
    it->second.started_at = SystemClock::GetCurrentTimestamp();

    // Run subagent in a separate thread
    running_threads_[agent_id] = std::thread([this, agent_id]() {
        auto& agent = subagents_[agent_id];
        RunSubagent(agent);
        running_threads_.erase(agent_id);
    });

    LOG_INFO("Started async subagent: {}", agent_id);
}

void SubagentCoordinator::RunSubagent(Subagent& agent) {
    LOG_INFO("Running subagent {}: {}", agent.id, agent.task);

    try {
        // Use singleton tool registry
        auto& tool_registry = ToolRegistry::GetInstance();

        // Create agent session for this subagent
        AgentSession session;
        session.use_tools = true;
        session.role = nullptr;
        // Set runtime dependencies
        session.tool_executor =
            [&tool_registry](const std::string& name, const nlohmann::json& args) {
                return tool_registry.ExecuteTool(name, args);
            };

        // Set provider (use default provider)
        auto& provider_router = ProviderRouter::GetInstance();
        session.provider = provider_router.GetDefaultProvider();

        // Build system prompt
        std::string system_prompt_text = subagent_system_prompt_ +
            "\n\nYou are working on: " + agent.description +
            "\n\nYour task: " + agent.task;

        session.system_prompt.push_back({"text", system_prompt_text, false});

        // Run the agent - using static Loop method
        AgentCore::Loop(agent.task, session);
        agent.messages = session.messages;

        // Extract result from final message
        if (!agent.messages.empty()) {
            const auto& last_msg = agent.messages.back();
            if (last_msg.role == "assistant") {
                agent.result = last_msg.text();
            }
        }

        agent.status = SubagentStatus::Completed;
        agent.completed_at = SystemClock::GetCurrentTimestamp();
        LOG_INFO("Subagent completed: {}", agent.id);

    } catch (const std::exception& e) {
        agent.status = SubagentStatus::Failed;
        agent.error = e.what();
        agent.completed_at = SystemClock::GetCurrentTimestamp();
        LOG_ERROR("Subagent failed: {} - {}", agent.id, e.what());
    }
}

const Subagent* SubagentCoordinator::GetSubagent(const std::string& agent_id) const {
    auto it = subagents_.find(agent_id);
    return (it != subagents_.end()) ? &it->second : nullptr;
}

std::vector<Subagent> SubagentCoordinator::GetAllSubagents() const {
    std::vector<Subagent> result;
    for (const auto& [id, agent] : subagents_) {
        result.push_back(agent);
    }
    return result;
}

bool SubagentCoordinator::CancelSubagent(const std::string& agent_id) {
    auto it = subagents_.find(agent_id);
    if (it == subagents_.end()) {
        return false;
    }

    if (it->second.status == SubagentStatus::Running) {
        // Try to find and stop the running thread
        auto thread_it = running_threads_.find(agent_id);
        if (thread_it != running_threads_.end()) {
            // Note: Thread cancellation is not directly supported in C++
            // The thread will continue running but we mark the agent as cancelled
        }
    }

    it->second.status = SubagentStatus::Cancelled;
    it->second.completed_at = SystemClock::GetCurrentTimestamp();
    LOG_INFO("Cancelled subagent: {}", agent_id);
    return true;
}

bool SubagentCoordinator::WaitForSubagent(const std::string& agent_id, int timeout_seconds) {
    auto start = std::chrono::steady_clock::now();

    while (true) {
        auto it = subagents_.find(agent_id);
        if (it == subagents_.end()) {
            return false;
        }

        if (it->second.status != SubagentStatus::Pending &&
            it->second.status != SubagentStatus::Running) {
            return true;  // Completed, failed, or cancelled
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > timeout_seconds) {
            LOG_ERROR("Timeout waiting for subagent: {}", agent_id);
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

std::string SubagentCoordinator::GetSubagentResult(const std::string& agent_id) const {
    auto it = subagents_.find(agent_id);
    if (it != subagents_.end()) {
        return it->second.result;
    }
    return "";
}

bool SubagentCoordinator::DeleteSubagent(const std::string& agent_id) {
    auto it = subagents_.find(agent_id);
    if (it == subagents_.end()) {
        return false;
    }

    if (it->second.status == SubagentStatus::Running) {
        LOG_WARN("Cannot delete running subagent: {}", agent_id);
        return false;
    }

    subagents_.erase(it);
    LOG_INFO("Deleted subagent: {}", agent_id);
    return true;
}

void SubagentCoordinator::ClearCompleted() {
    int cleared = 0;
    for (auto it = subagents_.begin(); it != subagents_.end();) {
        if (it->second.status == SubagentStatus::Completed ||
            it->second.status == SubagentStatus::Failed ||
            it->second.status == SubagentStatus::Cancelled) {
            it = subagents_.erase(it);
            cleared++;
        } else {
            ++it;
        }
    }
    LOG_INFO("Cleared {} completed subagents", cleared);
}

bool SubagentCoordinator::SendMessageToSubagent(const std::string& agent_id, const std::string& message) {
    auto it = subagents_.find(agent_id);
    if (it == subagents_.end()) {
        return false;
    }

    // Add message to subagent's conversation
    MessageSchema msg;
    msg.role = "user";
    msg.AddTextContent(message);
    it->second.messages.push_back(msg);

    LOG_DEBUG("Sent message to subagent {}: {}", agent_id, message);
    return true;
}

const std::vector<MessageSchema>& SubagentCoordinator::GetSubagentMessages(const std::string& agent_id) const {
    static std::vector<MessageSchema> empty;
    auto it = subagents_.find(agent_id);
    if (it != subagents_.end()) {
        return it->second.messages;
    }
    return empty;
}

int SubagentCoordinator::GetRunningCount() const {
    int count = 0;
    for (const auto& [id, agent] : subagents_) {
        if (agent.status == SubagentStatus::Running) {
            count++;
        }
    }
    return count;
}

int SubagentCoordinator::GetPendingCount() const {
    int count = 0;
    for (const auto& [id, agent] : subagents_) {
        if (agent.status == SubagentStatus::Pending) {
            count++;
        }
    }
    return count;
}

std::string SubagentCoordinator::LaunchSubagent(const std::string& prompt,
                                                 const std::string& subagent_type,
                                                 const std::string& model) {
    LOG_INFO("Launching subagent: type={}, model={}", subagent_type, model);

    // Create a subagent for this task
    std::string agent_id = CreateSubagent(
        subagent_type,  // name
        "Subagent task: " + subagent_type,  // description
        prompt  // task
    );

    // Set custom model if provided
    if (!model.empty()) {
        // Model would be used in RunSubagent, but for now we just log it
        LOG_INFO("Using model: {}", model);
    }

    // Start the subagent synchronously
    if (!StartSubagent(agent_id)) {
        return "Error: Failed to start subagent";
    }

    // Get result
    const Subagent* agent = GetSubagent(agent_id);
    if (!agent) {
        return "Error: Subagent disappeared";
    }

    if (agent->status == SubagentStatus::Failed) {
        return "Error: " + agent->error;
    }

    if (agent->status == SubagentStatus::Cancelled) {
        return "Subagent was cancelled";
    }

    // Clean up the subagent after completion
    std::string result = agent->result;
    DeleteSubagent(agent_id);

    return result;
}

}  // namespace prosophor
