// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>

#include "providers/llm_provider.h"

namespace prosophor {

/// Anthropic API provider implementation (supports any Anthropic-compatible API)
class AnthropicProvider : public LLMProvider {
 public:
    explicit AnthropicProvider() = default;

    ChatResponse ChatStream(const ChatRequest& request,
        std::function<void(StreamEvent, std::string)> callback) override;

    std::string GetProviderName() const override { return "anthropic"; }

    std::vector<std::string> GetSupportedModels() const override {
        return {"claude-sonnet-4-6", "claude-opus-4-6", "claude-haiku-4-5"};
    }

 protected:
    HeaderList CreateHeaders(const ChatRequest& request) const override;
    void PrintRequestLog(const ChatRequest& request) const override;

    std::string Serialize(const ChatRequest& request) const override;
    ChatResponse Deserialize(const std::string& json_str) const override;
};

}  // namespace prosophor
