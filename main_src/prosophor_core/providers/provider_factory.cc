// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/provider_factory.h"

#include "providers/llm/anthropic_provider.h"
#include "providers/llm/ollama_provider.h"
#include "providers/llm/openai_provider.h"
#ifdef PROSOPHOR_HAS_LOCAL_MODEL
#include "providers/llm/llamacpp_provider.h"
#endif
#ifdef PROSOPHOR_NUTRIBOT
#include "providers/llm/aidgen_provider.h"
#endif
#include "common/log_wrapper.h"

namespace prosophor {

// 远程 provider（anthropic/openai/ollama）是无状态的：base_url/api_key/model/
// timeout 由 ChatRequest（角色/会话级）按请求传入；只有本地 provider
// （llamacpp/aidgen）需要在构造期拿到模型路径等配置。
// [[maybe_unused]] 仅用于两种本地 provider 都被编译剔除的形态（满足 -Werror）。
std::unique_ptr<LLMProvider> LlmProviderFactory::CreateProvider(
    const std::string& type,
    [[maybe_unused]] const LlmProviderConfig& config) {
    if (type == "anthropic") {
        return std::make_unique<AnthropicProvider>();
    }

    if (type == "openai") {
        return std::make_unique<OpenAIProvider>();
    }

    if (type == "ollama") {
        return std::make_unique<OllamaProvider>();
    }

#ifdef PROSOPHOR_HAS_LOCAL_MODEL
    if (type == "llamacpp") {
        return std::make_unique<LlamacppProvider>(config.llamacpp_cfg);
    }
#endif

#ifdef PROSOPHOR_NUTRIBOT
    // 纯文本与多模态同为 AidGenProvider，且只此一个 entry：选哪份模型由
    // config_file 指向的模型配置决定——含 vlm_model 段即多模态，否则纯文本。
    // 故不存在 "aidgen" 与 "aidgen-vl" 两个 provider 争抢进程唯一 Context 的问题。
    if (type == "aidgen") {   // 构造即异步加载
        return std::make_unique<AidGenProvider>(config.aidgen_cfg);
    }
#endif

    LOG_WARN("Unknown provider type '{}', skipping", type);
    return nullptr;
}

}  // namespace prosophor
