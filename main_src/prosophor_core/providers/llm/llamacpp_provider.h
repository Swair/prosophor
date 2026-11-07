// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "providers/llm/llm_provider.h"
#include "config/config.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "llama.h"
#include "common/chat.h"
#pragma GCC diagnostic pop

namespace prosophor {

class LlamacppProvider : public LLMProvider {
public:
    explicit LlamacppProvider(const LlamacppModelConfig& cfg);
    ~LlamacppProvider() override;

    void Load() override;
    void Unload();
    bool IsLoaded() const override;
    bool IsLoading() const override;
    std::string GetLoadError() const override;

    std::string GetProviderName() const override { return "llamacpp"; }
    std::vector<std::string> GetSupportedModels() const override;
    std::string Serialize(const ChatRequest& request) const override;
    ChatResponse Deserialize(const std::string& json_str) const override;
    ChatResponse ChatStream(const ChatRequest& request,
        std::function<void(StreamEvent, std::string)> callback) override;

    ChatResponse Chat(const ChatRequest& request) override;

    /// Parse call:name{...} format from tool_text and add to response.
    void ParseToolCalls(ChatResponse& response, const std::string& tool_text);

public:
    LlamacppModelConfig cfg_;   // test-accessible (ProcessTokenFrame tests need marker config)
private:

    std::atomic<bool> loaded_{false};
    std::atomic<bool> loading_{false};
    std::string load_error_msg_;
    mutable std::mutex model_mutex_;
    std::future<void> load_future_;

public:
    // Test-accessible: thinking marker state (normally set by Load())
    bool        thinking_enabled_ = false;
    std::string thinking_start_;
    std::string thinking_end_;

private:
    llama_model*        model_   = nullptr;
    llama_context*      ctx_     = nullptr;
    llama_sampler*      sampler_ = nullptr;
    const llama_vocab*  vocab_   = nullptr;

    common_chat_templates_ptr chat_tmpls_;

    void Release();

    HeaderList CreateHeaders(const ChatRequest& request) const override;
    void PrintRequestLog(const ChatRequest& request) const override;
    common_chat_params BuildChatParams(const ChatRequest& request) const;
    std::string BuildFallbackPrompt(
        const std::vector<common_chat_msg>& messages) const;
    bool TokenizePrompt(const std::string& prompt, int max_tokens,
                        std::vector<llama_token>& out_tokens,
                        int& out_n_tokens, std::string& error_msg) const;
    bool PrefillPrompt(std::vector<llama_token>& prompt_tokens,
                       int n_prompt, ChatResponse& response);
    ChatResponse GenerateReply(
        const ChatRequest& request,
        std::function<void(StreamEvent, std::string)>& callback,
        int n_prompt);

public:
    /// Process a token frame: detect markers, emit stream events,
    /// and emit normal text directly based on thinking/content mode.
    /// Also handles streaming tool call detection.
    /// Returns true if a marker was found (caller should continue);
    /// false if caller should perform normal decode.
    bool ProcessTokenFrame(
        const std::string& raw,
        bool& thinking, bool& in_tool_call, bool& response_done,
        std::string& tool_call_buffer,
        ChatResponse& response,
        std::function<void(StreamEvent, std::string)>& callback);
};

}  // namespace prosophor
