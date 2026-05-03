// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/log_wrapper.h"
#include "tools/tool_registry.h"
#include "common/config.h"
#include "common/messages_schema.h"
#include "common/curl_client.h"

namespace prosophor {

/// Tool use information
struct ToolUseSchema {
    std::string id;
    std::string name;
    nlohmann::json arguments;
};

/// Token usage statistics
struct TokenUsageSchema {
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
};

/// Request for chat completion API (Anthropic-style structure)
struct ChatRequest {
    std::vector<SystemSchema> system;   // System messages with optional cache control
    std::vector<ToolsSchema> tools;       // Available tools
    std::vector<MessageSchema> messages;       // Conversation messages

    std::string model;
    int max_tokens = 8192;
    double temperature = 0.7;
    bool tool_choice_auto = true;
    bool stream = false;
    bool thinking = false;

    // Agent-specific base_url from config (set by BuildRequest from agent config)
    std::string base_url;
    // API key for this request (overrides provider's default key)
    std::string api_key;
    // Request timeout in seconds (overrides provider's default timeout)
    int timeout = 60;

    // System message helpers
    void AddSystemMessage(std::string text, bool cache_control = false) {
        system.push_back({"text", std::move(text), cache_control});
    }

    void AddSystemPrompt(std::string prompt) {
        AddSystemMessage(std::move(prompt), true);  // Cache the main system prompt
    }

    // Tool helpers
    void AddTool(const ToolsSchema& tool) {
        tools.push_back(tool);
    }

    void SetTools(std::vector<ToolsSchema> tool_list) {
        tools = std::move(tool_list);
    }

    // Message helpers
    void AddMessage(MessageSchema message) {
        messages.push_back(std::move(message));
    }

    void AddUserMessage(std::string text) {
        messages.emplace_back("user", std::move(text));
    }

    void AddAssistantMessage(std::string text) {
        messages.emplace_back("assistant", std::move(text));
    }

    void AddToolResultMessage(const std::string& tool_id, const std::string& result) {
        MessageSchema msg;
        msg.role = "user";  // Tool results come from user role in Anthropic API
        msg.AddToolResultContent(tool_id, result);
        messages.push_back(msg);
    }
};


/// Stream event for ChatStream callback
enum class StreamEvent {
    kThinkingStart,
    kThinkingDelta,
    kThinkingEnd,
    kContentStart,
    kContentDelta,
    kContentEnd,
    kError,
};

/// Response from chat completion API
struct ChatResponse {
    std::string content_thinking;
    std::string thinking_signature;  // API-required signature for thinking blocks
    std::string content_text;
    std::vector<ToolUseSchema> tool_calls;
    bool has_thinking = false;       // true if response contained a thinking block
    std::string stop_reason;
    std::string error_msg;  // Non-empty means the API returned an error
    TokenUsageSchema usage;

    // Convenience methods
    void AddThinking(std::string text) {
        if (!content_thinking.empty()) content_thinking += "\n";
        content_thinking += std::move(text);
    }
    void AddText(std::string text) {
        if (!content_text.empty()) content_text += "\n";
        content_text += std::move(text);
    }

    void AddToolCall(const std::string& id, const std::string& name, nlohmann::json args) {
        tool_calls.push_back({id, name, std::move(args)});
    }

    bool HasToolCalls() const {
        return !tool_calls.empty();
    }
};

// Token tracking
void RecordTokenUsage(const std::string& model, const TokenUsageSchema& usage);

/// Abstract interface for LLM providers
/// HTTP providers: Chat() is shared (serialize → POST → error check → deserialize → token tracking)
/// Each provider: Serialize(), Deserialize(), CreateHeaders(), PrintRequestLog(), ChatStream()
class LLMProvider {
 public:
    virtual ~LLMProvider() = default;

    ChatResponse Chat(const ChatRequest& request);

    /// Streaming chat - returns accumulated ChatResponse after stream completes
    virtual ChatResponse ChatStream(const ChatRequest& request,
      std::function<void(StreamEvent, std::string)> callback) = 0;

    virtual std::string GetProviderName() const = 0;

    virtual std::vector<std::string> GetSupportedModels() const = 0;

    virtual std::string Serialize(const ChatRequest& request) const = 0;

    virtual ChatResponse Deserialize(const std::string& json_str) const = 0;

 protected:
    /// Execute streaming HTTP POST request
    /// default_timeout: used when request.timeout <= 0 (Ollama: 180, others: 60)
    HttpResponse ExecuteStream(const ChatRequest& request,
        void* stream_handler,
        int default_timeout = 60) const;

    virtual HeaderList CreateHeaders(const ChatRequest& request) const = 0;
    virtual void PrintRequestLog(const ChatRequest& request) const = 0;
};

}  // namespace prosophor
