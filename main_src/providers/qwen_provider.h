// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>

#include "common/log_wrapper.h"

#include "common/curl_client.h"
#include "providers/llm_provider.h"

namespace aicode {

/// Qwen API provider implementation
class QwenProvider : public LLMProvider {
 public:
    explicit QwenProvider(const std::string& api_key,
                               const std::string& base_url = "https://dashscope.aliyuncs.com",
                               int timeout = 30);

    ChatResponse Chat(const ChatRequest& request) override;

    ChatResponse ChatStream(const ChatRequest& request,
        std::function<void(const ChatResponse&)> callback) override;

    std::string GetProviderName() const override { return "qwen"; }

    std::vector<std::string> GetSupportedModels() const override {
        return {"qwen-max", "qwen-plus", "qwen-turbo"};
    }

 protected:
    /// Print request log for debugging
    virtual void PrintRequestLog(const std::string& url) const;

    std::string Serialize(const ChatRequest& request) const override;
    ChatResponse Deserialize(const std::string& json_str) const override;

 private:
    HeaderList CreateHeaders() const;

    std::string api_key_;
    std::string base_url_;
    int timeout_;
};

}  // namespace aicode
