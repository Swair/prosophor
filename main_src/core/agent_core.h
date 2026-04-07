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
#include "core/agent_state.h"
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
/// Stateless singleton - all session state is passed via AgentState parameter
class AgentCore : public Noncopyable {
 public:
    static AgentCore& GetInstance();

    /// Initialize with service dependencies (call once at startup)
    void Initialize(std::shared_ptr<MemoryManager> memory_manager,
                    std::shared_ptr<SkillLoader> skill_loader,
                    std::vector<ToolsSchema> tool_schemas,
                    std::function<std::string(const std::string& tool_name, const nlohmann::json& args)> tool_executor);

    /// Process a message - adds user message to state, runs agent loop, returns assistant response
    void CloseLoop(const std::string& message, AgentState& state);

    /// Streaming version of CloseLoop
    std::vector<MessageSchema> LoopStream(const std::string& message, AgentState& state, AgentEventCallback callback);

    /// Stop the current agent turn
    void Stop();

    /// Set provider callbacks (for hot-reload provider switch)
    void SetProviderCallbacks(
        std::function<ChatResponse(const ChatRequest& request)> chat_cb,
        std::function<void(const ChatRequest&, std::function<void(const ChatResponse&)>)> stream_cb);

    /// Get compact service for context compaction
    CompactService& GetCompactService() { return CompactService::GetInstance(); }

    /// Get LSP manager for code intelligence
    void InitializeLsp();
    bool RequestLspForFile(const std::string& filepath);

 private:
    AgentCore();
    ~AgentCore();

    /// Build ChatRequest from AgentState
    ChatRequest BuildRequest(const AgentState& state);

    // Service dependencies (shared, read-only)
    std::shared_ptr<MemoryManager> memory_manager_;
    std::shared_ptr<SkillLoader> skill_loader_;
    std::vector<ToolsSchema> tool_schemas_;
    std::function<std::string(const std::string& tool_name, const nlohmann::json& args)> tool_executor_;

    // LLM callbacks (set by SessionManager/ProviderRouter)
    std::function<ChatResponse(const ChatRequest& request)> chat_llm_cb_;
    std::function<void(const ChatRequest&, std::function<void(const ChatResponse&)>)> chat_llm_stream_cb_;

    std::atomic<bool> stop_requested_;
};

}  // namespace aicode
