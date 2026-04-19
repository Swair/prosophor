// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/provider_router.h"

#include <shared_mutex>
#include <filesystem>

#include "common/log_wrapper.h"
#include "managers/agent_role_loader.h"

namespace aicode {

ProviderRouter& ProviderRouter::GetInstance() {
    static ProviderRouter instance;
    return instance;
}

void ProviderRouter::Initialize(const AiCodeConfig& config) {
    std::unique_lock<std::shared_mutex> lock(mutex_);  // 写锁

    providers_.clear();

    // Create providers based on config
    for (const auto& [name, provider_config] : config.providers) {
        try {
            auto provider = CreateProvider(name, provider_config);
            providers_[name] = provider;
            LOG_INFO("Initialized provider: {} ({})", name, provider_config.api_type);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to initialize provider {}: {}", name, e.what());
        }
    }

    // Set default provider based on default_role
    // Load the default role to get its provider_name
    std::string role_path = "config/.aicode/roles/" + config.default_role + ".md";
    if (std::filesystem::exists(role_path)) {
        auto& loader = AgentRoleLoader::GetInstance();
        try {
            AgentRole role = loader.LoadRole(role_path);
            if (!role.provider_name.empty()) {
                auto it = providers_.find(role.provider_name);
                if (it != providers_.end()) {
                    default_provider_ = it->second;
                    default_provider_name_ = role.provider_name;
                    LOG_INFO("Default provider '{}' set from default_role '{}'", default_provider_name_, config.default_role);
                }
            }
        } catch (const std::exception& e) {
            LOG_WARN("Failed to load default role '{}', using first provider: {}", config.default_role, e.what());
        }
    }

    // If no default set, use first available
    if (!default_provider_ && !providers_.empty()) {
        default_provider_ = providers_.begin()->second;
        default_provider_name_ = providers_.begin()->first;
        LOG_INFO("Using first available provider as default: {}", default_provider_name_);
    }
}

std::shared_ptr<LLMProvider> ProviderRouter::GetProvider(const std::string& /*role_id*/) {
    // For now, use default provider
    // TODO: Support role-specific provider mapping
    return GetDefaultProvider();
}

std::shared_ptr<LLMProvider> ProviderRouter::GetProviderByName(const std::string& provider_name) {
    std::shared_lock<std::shared_mutex> lock(mutex_);  // 读锁

    auto it = providers_.find(provider_name);
    if (it != providers_.end()) {
        return it->second;
    }

    LOG_WARN("Provider '{}' not found, using default", provider_name);
    return default_provider_;
}

std::shared_ptr<LLMProvider> ProviderRouter::GetDefaultProvider() {
    std::shared_lock<std::shared_mutex> lock(mutex_);  // 读锁
    return default_provider_;
}

std::string ProviderRouter::GetProviderName(const std::string& /*role_id*/) {
    // TODO: Support role-specific provider mapping
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return default_provider_name_;
}

std::shared_ptr<LLMProvider> ProviderRouter::CreateProvider(
    const std::string& type,
    const ProviderConfig& config) {

    if (type == "anthropic" || type == "claude") {
        return std::make_shared<AnthropicProvider>(
            config.api_key,
            config.base_url,
            config.timeout);
    }

    if (type == "qwen" || type == "dashscope") {
        return std::make_shared<QwenProvider>(
            config.api_key,
            config.base_url,
            config.timeout);
    }

    if (type == "ollama") {
        return std::make_shared<OllamaProvider>(
            config.base_url,
            config.timeout);
    }

    // Default to Qwen
    LOG_WARN("Unknown provider type '{}', defaulting to Qwen", type);
    return std::make_shared<QwenProvider>(
        config.api_key,
        config.base_url,
        config.timeout);
}

}  // namespace aicode
