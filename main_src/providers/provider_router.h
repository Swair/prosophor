// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <shared_mutex>

#include "providers/llm_provider.h"
#include "providers/anthropic_provider.h"
#include "providers/qwen_provider.h"
#include "providers/ollama_provider.h"
#include "common/config.h"

namespace aicode {

/// ProviderRouter: 根据角色配置路由到不同的 LLM Provider
class ProviderRouter {
public:
    static ProviderRouter& GetInstance();

    /// 初始化（从 config 加载 providers）
    void Initialize(const AiCodeConfig& config);

    /// 根据角色获取 provider
    std::shared_ptr<LLMProvider> GetProvider(const std::string& role_id);

    /// 根据 provider_name 获取 provider
    std::shared_ptr<LLMProvider> GetProviderByName(const std::string& provider_name);

    /// 获取默认 provider
    std::shared_ptr<LLMProvider> GetDefaultProvider();

    /// 获取 provider_name
    std::string GetProviderName(const std::string& role_id);

private:
    ProviderRouter() = default;

    mutable std::shared_mutex mutex_;  // C++17 读写锁
    std::unordered_map<std::string, std::shared_ptr<LLMProvider>> providers_;
    std::shared_ptr<LLMProvider> default_provider_;
    std::string default_provider_name_;

    /// 创建 provider 实例
    std::shared_ptr<LLMProvider> CreateProvider(
        const std::string& type,
        const ProviderConfig& config);
};

}  // namespace aicode
