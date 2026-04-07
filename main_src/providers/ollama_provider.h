// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <functional>

#include "providers/llm_provider.h"
#include "common/config.h"

namespace aicode {

/// Ollama provider for local LLM inference
class OllamaProvider : public LLMProvider {
 public:
    explicit OllamaProvider(const std::string& base_url = "http://localhost:11434",
                            int timeout_seconds = 120);

    ChatResponse Chat(const ChatRequest& request) override;

    void ChatStream(const ChatRequest& request,
                    std::function<void(const ChatResponse&)> callback) override;

    std::string GetProviderName() const override { return "ollama"; }

    std::vector<std::string> GetSupportedModels() const override;

    std::string Serialize(const ChatRequest& request) const override;

    ChatResponse Deserialize(const std::string& json_str) const override;

 private:
    std::string base_url_;
    int timeout_seconds_;

    nlohmann::json SerializeMessage(const MessageSchema& msg) const;
    nlohmann::json SerializeTools(const std::vector<ToolsSchema>& tools) const;
    void PrintRequestLog(const std::string& url) const;
};

}  // namespace aicode
