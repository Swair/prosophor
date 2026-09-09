// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/provider_router/llm_provider_router.h"

#include <filesystem>
#include <shared_mutex>

#include "common/file_utils.h"
#include "common/log_wrapper.h"
#include "managers/agent_role_loader.h"

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
        try {
            auto provider = CreateProvider(name, provider_config);
            if (!provider) {
                LOG_WARN("Skipped provider '{}' (unknown type)", name);
                continue;
            }
            providers_[name] = provider;
            LOG_DEBUG("Initialized provider: {}", name);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to initialize provider {}: {}", name, e.what());
        }
    }

    // Set default provider based on default_role
    // Load the default role to get its provider_name
    std::string primary_role = config.default_role.empty() ? "default" : config.default_role[0];
    std::string role_path = "config/.prosophor/roles/" + primary_role + ".json";
    if (FileExists(role_path)) {
        auto& loader = AgentRoleLoader::GetInstance();
        try {
            AgentRole role = loader.LoadRole(role_path);
            if (!role.provider_prot.empty()) {
                auto it = providers_.find(role.provider_prot);
                if (it != providers_.end()) {
                    default_provider_ = it->second;
                    default_provider_name_ = role.provider_prot;
                    LOG_DEBUG("Default provider '{}' set from default_role '{}'", default_provider_name_, primary_role);
                }
            }
        } catch (const std::exception& e) {
            LOG_WARN("Failed to load default role '{}', using first provider: {}", primary_role, e.what());
        }
    }

    // If no default set, use first available
    if (!default_provider_ && !providers_.empty()) {
        default_provider_ = providers_.begin()->second;
        default_provider_name_ = providers_.begin()->first;
        LOG_DEBUG("Using first available provider as default: {}", default_provider_name_);
    }

#ifdef PROSOPHOR_HAS_LOCAL_MODEL
    // Auto-start llamacpp (in-process) if configured
    auto llamacpp_it = providers_.find("llamacpp");
    if (llamacpp_it != providers_.end()) {
        auto* local = static_cast<LlamacppProvider*>(llamacpp_it->second.get());
        if (!local->IsLoaded() && !config.llamacpp_models.empty() &&
            config.llamacpp_models[0].auto_start) {
            local->Load();
        }
        // Also register as "local" alias for GetProviderByName lookups
        providers_["local"] = llamacpp_it->second;
    }
#endif
}

std::shared_ptr<LLMProvider> LlmProviderRouter::GetProvider(const std::string& role_id) {
    // 按角色路由：读取 config/.prosophor/roles/<role_id>.json 的 protocal
    if (!role_id.empty()) {
        const std::string role_path = "config/.prosophor/roles/" + role_id + ".json";
        if (FileExists(role_path)) {
            try {
                AgentRole role = AgentRoleLoader::GetInstance().LoadRole(role_path);
                if (!role.provider_prot.empty()) {
                    std::shared_lock<std::shared_mutex> lock(mutex_);
                    auto it = providers_.find(role.provider_prot);
                    if (it != providers_.end()) {
                        return it->second;
                    }
                }
            } catch (const std::exception& e) {
                LOG_WARN("GetProvider role '{}' load failed, fallback default: {}", role_id, e.what());
            }
        }
    }
    return GetDefaultProvider();
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

std::string LlmProviderRouter::GetProviderName(const std::string& /*role_id*/) {
    // TODO: Support role-specific provider mapping
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return default_provider_name_;
}

std::shared_ptr<LLMProvider> LlmProviderRouter::CreateProvider(
    const std::string& type,
    [[maybe_unused]] const ProviderConfig& config) {

    if (type == "anthropic") {
        return std::make_shared<AnthropicProvider>();
    }

    if (type == "openai") {
        return std::make_shared<OpenAIProvider>();
    }

    if (type == "ollama") {
        return std::make_shared<OllamaProvider>();
    }

#ifdef PROSOPHOR_HAS_LOCAL_MODEL
    if (type == "llamacpp") {
        return std::make_shared<LlamacppProvider>(config.llamacpp_cfg);
    }
#endif

#ifdef PROSOPHOR_NUTRIBOT
    if (type == "aidgen") {   // 构造即异步加载模型（与 llamacpp auto-start 同构）
        return std::make_shared<AidGenProvider>(
            AidGenModelConfig{ExpandHome(config.aidgen_config), AidGenBackend::Auto});
    }
#endif

    // Unknown protocol - return nullptr
    LOG_WARN("Unknown provider type '{}', skipping", type);
    return nullptr;
}

}  // namespace prosophor
