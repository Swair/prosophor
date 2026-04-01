// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "agents/subagent_coordinator.h"

#include <sstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "common/log_wrapper.h"
#include "common/constants.h"
#include "managers/memory_manager.h"
#include "core/skill_loader.h"

namespace aicode {

SubagentCoordinator& SubagentCoordinator::GetInstance() {
    static SubagentCoordinator instance;
    return instance;
}

void SubagentCoordinator::Initialize(ChatCallback chat_cb, StreamChatCallback stream_chat_cb) {
    chat_callback_ = chat_cb;
    stream_chat_callback_ = stream_chat_cb;
    LOG_INFO("SubagentCoordinator initialized");
}

std::string SubagentCoordinator::GenerateAgentId() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return "agent_" + std::to_string(millis);
}

std::string SubagentCoordinator::GetTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_r(&time_t_now, &tm_now);

    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
    return oss.str();
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
    agent.created_at = GetTimestamp();
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
    it->second.started_at = GetTimestamp();

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
    it->second.started_at = GetTimestamp();

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
        // Create agent core for subagent
        auto memory_manager = std::make_shared<MemoryManager>(std::filesystem::current_path());
        auto skill_loader = std::make_shared<SkillLoader>();

        // Create tool registry
        ToolRegistry tool_registry;
        tool_registry.RegisterBuiltinTools();

        // Create agent config for subagent
        AgentConfig config;
        config.model = "claude-sonnet-4-6";  // Default subagent model
        config.max_tokens = 4096;

        // Create agent core
        AgentCore agent_core(
            memory_manager,
            skill_loader,
            tool_registry.GetToolSchemas(),
            [&tool_registry](const std::string& name, const nlohmann::json& args) {
                return tool_registry.ExecuteTool(name, args);
            },
            chat_callback_,
            stream_chat_callback_,
            config
        );

        // Build system prompt
        std::string system_prompt_text = subagent_system_prompt_ +
            "\n\nYou are working on: " + agent.description +
            "\n\nYour task: " + agent.task;

        // Set system prompt and run the agent
        std::vector<SystemSchema> system_prompt;
        system_prompt.push_back({"text", system_prompt_text, false});
        agent_core.SetSystemPrompt(system_prompt, false);
        agent.messages = agent_core.CloseLoop(agent.task);

        // Extract result from final message
        if (!agent.messages.empty()) {
            const auto& last_msg = agent.messages.back();
            if (last_msg.role == "assistant") {
                agent.result = last_msg.text();
            }
        }

        agent.status = SubagentStatus::Completed;
        agent.completed_at = GetTimestamp();
        LOG_INFO("Subagent completed: {}", agent.id);

    } catch (const std::exception& e) {
        agent.status = SubagentStatus::Failed;
        agent.error = e.what();
        agent.completed_at = GetTimestamp();
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
    it->second.completed_at = GetTimestamp();
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

}  // namespace aicode
