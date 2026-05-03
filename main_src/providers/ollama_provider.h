// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <functional>

#include "providers/llm_provider.h"
#include "common/config.h"

namespace prosophor {

/// Ollama provider for local LLM inference
class OllamaProvider : public LLMProvider {
 public:
    explicit OllamaProvider();

    ChatResponse ChatStream(const ChatRequest& request,
                    std::function<void(StreamEvent, std::string)> callback) override;

    std::string GetProviderName() const override { return "ollama"; }

    std::vector<std::string> GetSupportedModels() const override;

    std::string Serialize(const ChatRequest& request) const override;
    ChatResponse Deserialize(const std::string& json_str) const override;

 protected:
    HeaderList CreateHeaders(const ChatRequest& request) const override;
    void PrintRequestLog(const ChatRequest& request) const override;

 private:
    nlohmann::json SerializeMessage(const MessageSchema& msg) const;
    nlohmann::json SerializeTools(const std::vector<ToolsSchema>& tools) const;
};

}  // namespace prosophor
