// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include "common/log_wrapper.h"

#include "common/noncopyable.h"
#include "common/config.h"
#include "common/constants.h"
#include "core/compact_service.h"
#include "core/messages_schema.h"
#include "providers/llm_provider.h"

namespace aicode {

// Forward declarations
class MemoryManager;
class SkillLoader;
class ToolRegistry;
struct SessionState;

/// Agent event for streaming responses
struct AgentEvent {
    std::string type;  // "text_delta" | "tool_use" | "tool_result" | "message_end"
    nlohmann::json data;
};

using AgentEventCallback = std::function<void(const AgentEvent&)>;

/// AgentCore: orchestrates message processing, tool execution, and LLM interaction
class AgentCore : public Noncopyable {
 public:
    AgentCore(std::shared_ptr<MemoryManager> memory_manager,
              std::shared_ptr<SkillLoader> skill_loader,
              std::vector<ToolsSchema> tool_schemas,
              std::function<std::string(const std::string& tool_name, const nlohmann::json& args)> tool_executor,
              std::function<ChatResponse(const ChatRequest& request)> chat_completion,
              std::function<void(const ChatRequest&, std::function<void(const ChatResponse&)>)> chat_completion_stream,
              const AgentConfig& agent_config);

    /// Process a message - adds user message to internal history, runs agent loop, returns assistant response
    std::vector<MessageSchema> CloseLoop(const std::string& message);

    /// Streaming version of CloseLoop
    std::vector<MessageSchema> LoopStream(const std::string& message, AgentEventCallback callback);

    /// Stop the current agent turn
    void Stop();

    /// Get current conversation history
    const std::vector<MessageSchema>& GetHistory() const { return chat_request_.messages; }

    /// Get current conversation history (non-const version for modification)
    std::vector<MessageSchema>& GetHistory() { return chat_request_.messages; }

    /// Clear conversation history
    void ClearHistory() { chat_request_.messages.clear(); }

    /// Set max iterations
    void SetMaxIterations(int max) { max_iterations_ = max; }

    /// Update agent config (for hot-reload)
    void SetConfig(const AgentConfig& config);

    /// Get current config
    const AgentConfig& GetConfig() const { return agent_config_; }

    /// Set model dynamically
    void SetModel(const std::string& model_ref);

    /// Set system prompt
    void SetSystemPrompt(const std::vector<SystemSchema>& system_prompt, bool is_cache = true);

    /// Get compact service for context compaction
    CompactService& GetCompactService() { return CompactService::GetInstance(); }

    /// Get LSP manager for code intelligence
    void InitializeLsp();
    bool RequestLspForFile(const std::string& filepath);

 private:
    std::shared_ptr<MemoryManager> memory_manager_;
    std::shared_ptr<SkillLoader> skill_loader_;
    std::vector<ToolsSchema> tool_schemas_;
    std::function<std::string(const std::string& tool_name, const nlohmann::json& args)> tool_executor_;

    std::function<ChatResponse(const ChatRequest& request)> chat_llm_cb_;
    std::function<void(const ChatRequest&, std::function<void(const ChatResponse&)>)> chat_llm_stream_cb_;

    AgentConfig agent_config_;
    std::atomic<bool> stop_requested_;
    int max_iterations_;


    ChatRequest chat_request_;
};

}  // namespace aicode
