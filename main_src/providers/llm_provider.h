// Copyright 2026 AiCode Contributors
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
#include "core/messages_schema.h"
#include "core/agent_state.h"  // For SystemSchema

namespace aicode {

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
    std::string thinking = "off";  // "off" | "low" | "medium" | "high"

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


/// Response from chat completion API
struct ChatResponse {
    // std::string content_thinking;
    std::string content_text;
    std::vector<ToolUseSchema> tool_calls;
    bool is_stream_end = false;
    // std::string id;
    // std::string model;
    // std::string role;
    std::string stop_reason;
    // std::string type;
    TokenUsageSchema usage;

    // Convenience methods
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
class LLMProvider {
 public:
    virtual ~LLMProvider() = default;

    virtual ChatResponse Chat(const ChatRequest& request) = 0;

    virtual void ChatStream(const ChatRequest& request,
      std::function<void(const ChatResponse&)> callback) = 0;

    virtual std::string GetProviderName() const = 0;

    virtual std::vector<std::string> GetSupportedModels() const = 0;

    virtual std::string Serialize(const ChatRequest& request) const = 0;

    virtual ChatResponse Deserialize(const std::string& json_str) const = 0;

};

}  // namespace aicode
