// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/llm_provider_router.h"

#include <algorithm>
#include <filesystem>
#include <shared_mutex>

#include "common/log_wrapper.h"
#include "providers/provider_factory.h"

namespace prosophor {

LlmProviderRouter& LlmProviderRouter::GetInstance() {
    static LlmProviderRouter instance;
    return instance;
}

void LlmProviderRouter::Initialize(const ProsophorConfig& config) {
    std::unique_lock<std::shared_mutex> lock(mutex_);  // 写锁

    providers_.clear();

    // Create providers based on config
    for (const auto& [name, provider_config] : config.llm_providers) {
        if (!provider_config.enabled) {
            LOG_INFO("Skipped provider '{}' (enabled=false)", name);
            continue;
        }
        try {
            auto provider = LlmProviderFactory::CreateProvider(name, provider_config);
            if (!provider) {
                LOG_WARN("Skipped provider '{}' (unknown type)", name);
                continue;
            }
            providers_[name] = std::move(provider);
            LOG_DEBUG("Initialized provider: {}", name);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to initialize provider {}: {}", name, e.what());
        }
    }

    // Deterministic fallback: smallest provider name. AgentEngine 随后会用
    // default_role[0] 的 provider_prot 调 SetDefaultProvider 覆盖。
    if (!providers_.empty()) {
        auto it = std::min_element(
            providers_.begin(), providers_.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
        default_provider_ = it->second;
        default_provider_name_ = it->first;
        LOG_DEBUG("Fallback default provider: '{}'", default_provider_name_);
    }

#ifdef PROSOPHOR_HAS_LOCAL_MODEL
    // Auto-start llamacpp (in-process) if configured
    auto llamacpp_it = providers_.find("llamacpp");
    if (llamacpp_it != providers_.end()) {
        if (!llamacpp_it->second->IsLoaded() && !config.llamacpp_models.empty() &&
            config.llamacpp_models[0].auto_start) {
            llamacpp_it->second->Load();
        }
        // Also register as "local" alias for GetProviderByName lookups
        providers_["local"] = llamacpp_it->second;
    }
#endif
}

std::shared_ptr<LLMProvider> LlmProviderRouter::GetProviderByName(const std::string& provider_name) {
    std::shared_lock<std::shared_mutex> lock(mutex_);  // 读锁

    auto it = providers_.find(provider_name);
    if (it != providers_.end()) {
        return it->second;
    }

    LOG_WARN("Provider '{}' not found, using default", provider_name);
    return default_provider_;
}

std::shared_ptr<LLMProvider> LlmProviderRouter::GetDefaultProvider() {
    std::shared_lock<std::shared_mutex> lock(mutex_);  // 读锁
    return default_provider_;
}

std::shared_ptr<LLMProvider> LlmProviderRouter::GetProviderByNameExact(
    const std::string& provider_name) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = providers_.find(provider_name);
    return it == providers_.end() ? nullptr : it->second;
}

void LlmProviderRouter::SetDefaultProvider(const std::string& provider_name) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = providers_.find(provider_name);
    if (it == providers_.end()) {
        LOG_WARN("Default provider '{}' is not configured; keeping '{}'",
                 provider_name, default_provider_name_);
        return;
    }
    default_provider_ = it->second;
    default_provider_name_ = provider_name;
    LOG_DEBUG("Default provider set to '{}'", default_provider_name_);
}

}  // namespace prosophor
