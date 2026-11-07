// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <shared_mutex>

#include "providers/llm/llm_provider.h"
#include "config/config.h"

namespace prosophor {

/// LlmProviderRouter: provider 注册表 + 切换管理。
/// 实例创建交给 LlmProviderFactory；本类只负责持有、按名/角色路由与默认选择。
class LlmProviderRouter {
public:
    static LlmProviderRouter& GetInstance();

    /// 初始化（从 config 加载 providers）
    void Initialize(const ProsophorConfig& config);

    /// 根据 provider_name 获取 provider（不存在时回退默认，会话路径使用）
    std::shared_ptr<LLMProvider> GetProviderByName(const std::string& provider_name);

    /// 严格按名查找（不存在返回 nullptr，一次性调用/状态查询使用）
    std::shared_ptr<LLMProvider> GetProviderByNameExact(const std::string& provider_name) const;

    /// 获取默认 provider
    std::shared_ptr<LLMProvider> GetDefaultProvider();

    /// 由 AgentEngine 用已加载角色的 provider_prot 覆盖默认 provider
    void SetDefaultProvider(const std::string& provider_name);

private:
    LlmProviderRouter() = default;

    mutable std::shared_mutex mutex_;  // C++17 读写锁
    std::unordered_map<std::string, std::shared_ptr<LLMProvider>> providers_;
    std::shared_ptr<LLMProvider> default_provider_;
    std::string default_provider_name_;
};

}  // namespace prosophor
