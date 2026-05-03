// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>

#include "providers/llm_provider.h"

namespace prosophor {

/// OpenAI-compatible API provider
/// Protocol: POST {base_url}/v1/chat/completions
/// SSE: data: {choices[{delta:{reasoning_content, content}}]} | [DONE]
class OpenAIProvider : public LLMProvider {
 public:
    explicit OpenAIProvider(bool enable_thinking = false);

    ChatResponse ChatStream(const ChatRequest& request,
        std::function<void(StreamEvent, std::string)> callback) override;

    std::string GetProviderName() const override { return "openai"; }

    std::vector<std::string> GetSupportedModels() const override {
        return {"deepseek-v4-flash"};
    }

    /// Set provider name for display (e.g. "qwen", "deepseek")
    void SetDisplayName(const std::string& name) { display_name_ = name; }

 protected:
    HeaderList CreateHeaders(const ChatRequest& request) const override;
    void PrintRequestLog(const ChatRequest& request) const override;

    std::string Serialize(const ChatRequest& request) const override;
    ChatResponse Deserialize(const std::string& json_str) const override;

 private:
    nlohmann::json SerializeMessageContent(const std::vector<ContentSchema>& content) const;
    nlohmann::json SerializeTools(const std::vector<ToolsSchema>& tools) const;

    bool enable_thinking_;
    std::string display_name_ = "openai";
};

}  // namespace prosophor
