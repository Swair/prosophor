// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "managers/agent_role_loader.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

#include "common/file_utils.h"
#include "common/log_wrapper.h"
#include "config/config.h"
#include "tools/tool_registry.h"
#include "managers/skill_loader.h"
#include "core/dialog_strategy.h"

namespace prosophor {

AgentRoleLoader& AgentRoleLoader::GetInstance() {
    static AgentRoleLoader instance;
    return instance;
}

AgentRole AgentRoleLoader::LoadRole(const std::string& role_path) {
    std::ifstream ifs(role_path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open role file: " + role_path);
    }

    nlohmann::json j;
    try {
        ifs >> j;
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse role JSON: " + role_path + " — " + e.what());
    }

    std::string file_stem = std::filesystem::path(role_path).stem().string();
    return ParseFromJson(j, file_stem);
}

std::vector<AgentRole> AgentRoleLoader::LoadAllRoles(const std::string& roles_dir) {
    std::vector<AgentRole> roles;

    if (!DirExists(roles_dir)) {
        LOG_WARN("Roles directory does not exist: {}", roles_dir);
        return roles;
    }

    for (const auto& entry : std::filesystem::directory_iterator(roles_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            try {
                AgentRole role = LoadRole(entry.path().string());
                roles.push_back(role);
                LOG_DEBUG("Loaded role: {} ({})", role.name, role.id);
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to load role {}: {}", entry.path().string(), e.what());
            }
        }
    }

    return roles;
}

AgentRole AgentRoleLoader::ParseFromJson(const nlohmann::json& j, const std::string& file_stem) const {
    AgentRole role;
    role.id = j.value("id", file_stem);
    role.name = j.value("role_name", j.value("name", role.id));
    role.description = j.value("description", "");
    role.sprite_id = j.value("sprite_id", "");
    role.sprite_assets_dir = j.value("sprite_assets_dir", "");
    if (auto tts = j.find("tts"); tts != j.end() && tts->is_object()) {
        role.tts_backend = tts->value("provider", tts->value("backend", ""));
        role.tts_voice = tts->value("voice", "");
        role.tts_auto_start = tts->value("auto_start", tts->value("autoStart", false));
    }

    // Read provider/model from "llm" block
    const nlohmann::json* llm = nullptr;
    if (auto it = j.find("llm"); it != j.end() && it->is_object()) llm = &*it;
    role.provider_prot = llm ? llm->value("protocal", "") : j.value("provider_prot", "");
    role.model = llm ? llm->value("model", std::string("")) : j.value("model", std::string(""));
    // Role-level thinking: if explicitly set in role JSON, it takes priority over model config
    if (llm && llm->contains("thinking"))
        role.thinking = (*llm)["thinking"].get<bool>();
    if (llm && llm->contains("thinking_budget_tokens"))
        role.thinking_budget_tokens = (*llm)["thinking_budget_tokens"].get<int>();
    if (llm && llm->contains("reasoning_effort"))
        role.reasoning_effort = (*llm)["reasoning_effort"].get<std::string>();

    // Support combined "provider:model" format in model field
    if (!role.model.empty()) {
        auto colon_pos = role.model.find(':');
        if (colon_pos != std::string::npos) {
            std::string extracted_provider = role.model.substr(0, colon_pos);
            std::string extracted_model = role.model.substr(colon_pos + 1);
            if (role.provider_prot.empty()) {
                role.provider_prot = extracted_provider;
                role.model = extracted_model;
            }
        }
    }

    // Resolve provider and agent config
    auto& config = ProsophorConfig::GetInstance();
    std::string provider_to_use = role.provider_prot;

    if (provider_to_use.empty()) {
        std::string primary_role = config.default_role.empty() ? "default" : config.default_role[0];
        std::string default_role_path = "config/.prosophor/roles/" + primary_role + ".json";
        if (FileExists(default_role_path)) {
            auto& loader = AgentRoleLoader::GetInstance();
            try {
                AgentRole default_role = loader.LoadRole(default_role_path);
                if (!default_role.provider_prot.empty()) {
                    provider_to_use = default_role.provider_prot;
                    LOG_DEBUG("Role using default provider '{}' from default_role '{}'",
                             role.id, provider_to_use, primary_role);
                }
            } catch (const std::exception& e) {
                LOG_WARN("Failed to load default role '{}', using fallback: {}", primary_role, e.what());
            }
        }

        if (provider_to_use.empty() && !config.llm_providers.empty()) {
            provider_to_use = config.llm_providers.begin()->first;
            LOG_DEBUG("Role using first available provider: {}", role.id, provider_to_use);
        }
    }

    // Resolve model/agent config from provider
    if (!provider_to_use.empty()) {
        auto provider_it = config.llm_providers.find(provider_to_use);
        if (provider_it != config.llm_providers.end()) {
            auto& model_configs = provider_it->second.model_configs;

            // Try "{provider}/{model}" first, then bare model name
            auto model_it = model_configs.find(provider_to_use + "/" + role.model);
            if (model_it == model_configs.end()) {
                model_it = model_configs.find(role.model);
            }
            if (model_it == model_configs.end() && !model_configs.empty()) {
                // Fallback: use first available model
                model_it = model_configs.begin();
            }

            if (model_it != model_configs.end()) {
                role.temperature = model_it->second.temperature;
                role.max_tokens = model_it->second.max_tokens;
                role.context_window = model_it->second.context_window;
                role.model = model_it->second.model;
                role.enable_streaming = model_it->second.enable_streaming;
                // Model config's thinking is a fallback; role JSON's thinking takes priority
                if (!llm || !llm->contains("thinking"))
                    role.thinking = model_it->second.thinking;
                if (!llm || !llm->contains("thinking_budget_tokens"))
                    role.thinking_budget_tokens = model_it->second.thinking_budget_tokens;
                if (!llm || !llm->contains("reasoning_effort"))
                    role.reasoning_effort = model_it->second.reasoning_effort;
                LOG_DEBUG("Role '{}' using model='{}' from provider '{}': temperature={}, max_tokens={}, context_window={}, enable_streaming={}, thinking={}, thinking_budget_tokens={}, reasoning_effort={}",
                         role.id, model_it->second.model, provider_to_use,
                         role.temperature, role.max_tokens, role.context_window, role.enable_streaming,
                         role.thinking, role.thinking_budget_tokens, role.reasoning_effort);
            } else {
                LOG_WARN("Role {}: no models configured in provider '{}', using hardcoded defaults", role.id, provider_to_use);
            }
        }
    }

    // 性格配置
    role.personality = j.value("personality", "default");
    role.soul = j.value("soul", "");
    role.role_system_prompt = j.value("system_prompt", "");

    // sprite_id → 查找 sprite JSON，同名覆盖 role 字段
    if (!role.sprite_id.empty()) {
        nlohmann::json sj;  // sprite JSON overlay

        // 1. config sprite_assets_dir/{sprite_id}/meta.json
        if (!config.sprite_assets_dir.empty()) {
            std::string meta_path = prosophor::JoinPath(config.sprite_assets_dir, role.sprite_id, "meta.json");
            std::ifstream mf(meta_path);
            if (mf) { try { mf >> sj; } catch (...) {} }
        }

        // 2. Override role fields with sprite fields (同名覆盖)
        if (!sj.empty()) {
            if (auto it = sj.find("display_name"); it != sj.end() && it->is_string())
                role.name = it->get<std::string>();
            if (auto it = sj.find("description"); it != sj.end() && it->is_string())
                role.description = it->get<std::string>();
            if (auto it = sj.find("personality"); it != sj.end() && it->is_string())
                role.personality = it->get<std::string>();
            if (auto it = sj.find("soul"); it != sj.end() && it->is_string())
                role.soul = it->get<std::string>();
            if (auto it = sj.find("system_prompt"); it != sj.end() && it->is_string())
                role.role_system_prompt = it->get<std::string>();
            LOG_DEBUG("Sprite overlay applied for sprite_id='{}': name='{}', desc='{}', personality='{}'",
                      role.sprite_id, role.name, role.description, role.personality);
        }
    }

    // Read LLM-specific fields (skills, tools, etc.) from "llm" block
    const nlohmann::json* llm_config = llm ? llm : &j;

    // 技能配置 - 支持通配符 "*"
    if (llm_config->contains("skills_white_list")) {
        auto skills_config = (*llm_config)["skills_white_list"];
        if (skills_config.is_array() && skills_config.size() == 1 &&
            skills_config[0] == "*") {
            auto& skill_loader = SkillLoader::GetInstance();
            role.skills = skill_loader.GetAllSkillIds();
            LOG_DEBUG("Role uses all skills: {}", role.id, role.skills.size());
        } else if (skills_config.is_array()) {
            role.skills = skills_config.get<std::vector<std::string>>();
        } else if (skills_config.is_string()) {
            role.skills.push_back(skills_config.get<std::string>());
        }
    }

    // 工具配置 - 支持通配符 "*"
    bool tools_explicitly_configured = llm_config->contains("tools_white_list");
    if (tools_explicitly_configured) {
        auto tools_config = (*llm_config)["tools_white_list"];
        if (tools_config.is_array() && tools_config.size() == 1 &&
            tools_config[0] == "*") {
            auto& tool_registry = ToolRegistry::GetInstance();
            role.tools = tool_registry.GetToolSchemas();
            role.auto_confirm_tools = true;
            LOG_DEBUG("Role uses all tools with auto_confirm=true: {}", role.id, role.tools.size());
        } else if (tools_config.is_array()) {
            auto& tool_registry = ToolRegistry::GetInstance();
            auto all_tools = tool_registry.GetToolSchemas();
            std::vector<std::string> tool_names = tools_config.get<std::vector<std::string>>();
            for (const auto& tool : all_tools) {
                if (std::find(tool_names.begin(), tool_names.end(), tool.name) != tool_names.end()) {
                    role.tools.push_back(tool);
                }
            }
        } else if (tools_config.is_string()) {
            auto& tool_registry = ToolRegistry::GetInstance();
            auto all_tools = tool_registry.GetToolSchemas();
            std::string tool_name = tools_config.get<std::string>();
            for (const auto& tool : all_tools) {
                if (tool.name == tool_name) {
                    role.tools.push_back(tool);
                    break;
                }
            }
        }
    }

    role.max_iterations = llm_config->value("max_iterations", 15);
    role.enable_tools = llm_config->value("enable_tools", true);
    role.auto_confirm_tools = llm_config->value("auto_confirm_tools", false);
    role.enable_streaming = llm_config->value("enable_streaming", true);
    role.enable_agent_chat = llm_config->value("enable_agent_chat", false);
    role.enable_summary = llm_config->value("enable_summary", true);

    role.memory_dir = llm_config->value("memory_dir", std::string(""));
    if (role.memory_dir.empty()) {
        role.memory_dir = ExpandHome(
            "~/.prosophor/memories/" + role.id);
    }

    // 默认加载所有工具（如果没有显式指定 tools_white_list）
    if (!tools_explicitly_configured && role.tools.empty()) {
        auto& tool_registry = ToolRegistry::GetInstance();
        role.tools = tool_registry.GetToolSchemas();
    }

    // 如果禁用了工具，清空工具列表
    if (!role.enable_tools) {
        role.tools.clear();
    }

    // 初始化默认策略
    role.dialog_strategy = DialogStrategy::CreateDefault();
    role.memory_strategy = CreateDefaultMemoryStrategy();

    return role;
}

}  // namespace prosophor
