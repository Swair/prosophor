// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "managers/agent_role_loader.h"

#include <fstream>
#include <sstream>
#include <cctype>
#include <filesystem>
#include <algorithm>

#include "common/log_wrapper.h"
#include "common/config.h"
#include "tools/tool_registry.h"
#include "managers/skill_loader.h"

namespace aicode {

AgentRoleLoader& AgentRoleLoader::GetInstance() {
    static AgentRoleLoader instance;
    return instance;
}

AgentRole AgentRoleLoader::LoadRole(const std::string& role_path) {
    return ParseMarkdownFile(role_path);
}

std::vector<AgentRole> AgentRoleLoader::LoadAllRoles(const std::string& roles_dir) {
    std::vector<AgentRole> roles;

    if (!std::filesystem::exists(roles_dir)) {
        LOG_WARN("Roles directory does not exist: {}", roles_dir);
        return roles;
    }

    for (const auto& entry : std::filesystem::directory_iterator(roles_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".md") {
            try {
                AgentRole role = ParseMarkdownFile(entry.path());
                roles.push_back(role);
                LOG_INFO("Loaded role: {} ({})", role.name, role.id);
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to load role {}: {}", entry.path().string(), e.what());
            }
        }
    }

    return roles;
}

std::string AgentRoleLoader::ExtractFrontmatter(const std::string& content,
                                                  std::string& body) const {
    if (content.size() < 4 || content.substr(0, 3) != "---") {
        body = content;
        return "";
    }

    size_t end_pos = content.find("\n---\n", 3);
    if (end_pos == std::string::npos) {
        body = content;
        return "";
    }

    std::string frontmatter = content.substr(3, end_pos - 3);
    body = content.substr(end_pos + 4);

    // Trim leading/trailing whitespace from body
    while (!body.empty() && (body.front() == ' ' || body.front() == '\t' || body.front() == '\n')) {
        body.erase(0, 1);
    }

    return frontmatter;
}

AgentRole AgentRoleLoader::ParseMarkdownFile(const std::filesystem::path& file) const {
    std::ifstream ifs(file);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open role file: " + file.string());
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string content = buffer.str();

    std::string body;
    std::string frontmatter_str = ExtractFrontmatter(content, body);

    if (frontmatter_str.empty()) {
        throw std::runtime_error("Invalid role file format (missing frontmatter): " + file.string());
    }

    nlohmann::json metadata = ParseYamlFrontmatter(frontmatter_str);
    LOG_INFO("ParseMarkdownFile: file={}, metadata keys: {}", file.string(), metadata.dump());

    AgentRole role;
    role.id = metadata.value("id", file.stem().string());
    role.name = metadata.value("name", role.id);
    role.description = metadata.value("description", "");
    role.avatar = metadata.value("avatar", "🤖");

    // Provider 配置（可选，空则使用全局默认）
    role.provider_name = metadata.value("provider_name", "");
    role.model = metadata.value("model", std::string(""));

    // 从 settings.json 的 agents 配置中读取 temperature 和 max_tokens
    // 如果 role 中没有指定 provider_name，则根据 default_role 来确定默认 provider
    auto& config = AiCodeConfig::GetInstance();
    std::string provider_to_use = role.provider_name;

    // 如果 role 没有指定 provider_name，使用 default_role 的 provider
    if (provider_to_use.empty()) {
        // 尝试加载 default_role 来获取其 provider_name
        std::string default_role_path = "config/.aicode/roles/" + config.default_role + ".md";
        if (std::filesystem::exists(default_role_path)) {
            auto& loader = AgentRoleLoader::GetInstance();
            try {
                AgentRole default_role = loader.LoadRole(default_role_path);
                if (!default_role.provider_name.empty()) {
                    provider_to_use = default_role.provider_name;
                    LOG_INFO("Role {} using default provider '{}' from default_role '{}'",
                             role.id, provider_to_use, config.default_role);
                }
            } catch (const std::exception& e) {
                LOG_WARN("Failed to load default role '{}', using fallback: {}", config.default_role, e.what());
            }
        }

        // 如果还是没有找到 provider，使用 providers 中的第一个
        if (provider_to_use.empty() && !config.providers.empty()) {
            provider_to_use = config.providers.begin()->first;
            LOG_INFO("Role {} using first available provider: {}", role.id, provider_to_use);
        }
    }

    // 尝试从 provider 的 agents 中查找匹配的 model key
    // role.model 有两种含义：
    // 1. agent key (如 "default") → 查找 agents 配置
    // 2. 直接模型名称 (如 "qwen3.5-plus") → 使用默认 agent 配置 + 自定义模型名称
    if (!provider_to_use.empty()) {
        auto provider_it = config.providers.find(provider_to_use);
        if (provider_it != config.providers.end()) {
            std::string agent_key = role.model;  // 保存原始 model 值

            // 尝试将 role.model 作为 agent key 查找
            auto agent_it = provider_it->second.agents.find(role.model);
            if (agent_it != provider_it->second.agents.end()) {
                // 找到了匹配的 agent key，使用 agent 配置
                role.temperature = agent_it->second.temperature;
                role.max_tokens = agent_it->second.max_tokens;
                role.model = agent_it->second.model;  // 使用 agent 配置中的实际模型名称
                role.enable_streaming = agent_it->second.enable_streaming;  // 读取 stream 配置
                LOG_INFO("Role {} using agent '{}' from provider '{}': model={}, temperature={}, max_tokens={}, enable_streaming={}",
                         role.id, agent_key, provider_to_use, agent_it->second.model,
                         role.temperature, role.max_tokens, role.enable_streaming);
            } else {
                // 没有找到匹配的 agent key，使用默认 agent 配置 + 原始模型名称
                auto default_agent_it = provider_it->second.agents.find("default");
                if (default_agent_it != provider_it->second.agents.end()) {
                    role.temperature = default_agent_it->second.temperature;
                    role.max_tokens = default_agent_it->second.max_tokens;
                    role.enable_streaming = default_agent_it->second.enable_streaming;  // 读取默认 agent 的 stream 配置
                    // role.model 保持原始值（如 "qwen3.5-plus"）
                    LOG_INFO("Role {} using default agent config with model '{}' from provider '{}': temperature={}, max_tokens={}, enable_streaming={}",
                             role.id, role.model, provider_to_use, role.temperature, role.max_tokens, role.enable_streaming);
                } else {
                    LOG_WARN("Role {}: no 'default' agent in provider '{}', using hardcoded defaults", role.id, provider_to_use);
                }
            }
        }
    }

    // 性格配置
    role.personality = metadata.value("personality", "default");
    role.personality_prompt = metadata.value("personality_prompt", "");
    role.role_system_prompt = metadata.value("system_prompt", "");

    // 技能配置 - 支持通配符 "*" 和兼容字符串格式
    if (metadata.contains("skills_white_list")) {
        auto skills_config = metadata["skills_white_list"];
        if (skills_config.is_array() && skills_config.size() == 1 &&
            skills_config[0] == "*") {
            // 通配符：加载所有技能
            auto& skill_loader = SkillLoader::GetInstance();
            role.skills = skill_loader.GetAllSkillIds();
            LOG_INFO("Role {} uses all skills: {}", role.id, role.skills.size());
        } else if (skills_config.is_array()) {
            role.skills = skills_config.get<std::vector<std::string>>();
        } else if (skills_config.is_string()) {
            // 兼容字符串格式
            role.skills.push_back(skills_config.get<std::string>());
        }
    }

    // 工具配置 - 支持通配符 "*" 和兼容字符串格式
    // 记录是否显式配置了 tools_white_list 字段（用于自动设置 use_tools）
    bool tools_explicitly_configured = metadata.contains("tools_white_list");
    LOG_INFO("Role {}: tools_explicitly_configured={}, metadata.has('tools_white_list')={}",
             role.id, tools_explicitly_configured, metadata.contains("tools_white_list"));
    if (tools_explicitly_configured) {
        LOG_INFO("Role {}: tools_config = {}", role.id, metadata["tools_white_list"].dump());
        LOG_INFO("Role {}: tools_config.is_array={}, tools_config.type={}",
                 role.id, metadata["tools_white_list"].is_array(), metadata["tools_white_list"].type_name());
    }

    if (tools_explicitly_configured) {
        auto tools_config = metadata["tools_white_list"];
        if (tools_config.is_array() && tools_config.size() == 1 &&
            tools_config[0] == "*") {
            // 通配符：加载所有工具，并启用自动确认（跳过 Permission Required）
            auto& tool_registry = ToolRegistry::GetInstance();
            role.tools = tool_registry.GetToolSchemas();
            role.auto_confirm_tools = true;  // 关键：启用自动确认，不弹权限询问
            LOG_INFO("Role {} uses all tools with auto_confirm=true: {}", role.id, role.tools.size());
        } else if (tools_config.is_array()) {
            // 工具名称列表
            auto& tool_registry = ToolRegistry::GetInstance();
            auto all_tools = tool_registry.GetToolSchemas();
            std::vector<std::string> tool_names = tools_config.get<std::vector<std::string>>();
            for (const auto& tool : all_tools) {
                if (std::find(tool_names.begin(), tool_names.end(), tool.name) != tool_names.end()) {
                    role.tools.push_back(tool);
                }
            }
        } else if (tools_config.is_string()) {
            // 兼容字符串格式：单个工具名
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

    role.max_iterations = metadata.value("max_iterations", 15);
    role.auto_confirm_tools = metadata.value("auto_confirm_tools", false);
    role.enable_streaming = metadata.value("enable_streaming", true);

    role.memory_dir = metadata.value("memory_dir", std::string(""));

    // 默认加载所有工具（如果没有显式指定 tools_white_list）
    if (!tools_explicitly_configured && role.tools.empty()) {
        auto& tool_registry = ToolRegistry::GetInstance();
        role.tools = tool_registry.GetToolSchemas();
    }

    // Store body content as additional role system prompt if present
    if (!body.empty()) {
        if (!role.role_system_prompt.empty()) {
            role.role_system_prompt += "\n\n";
        }
        role.role_system_prompt += body;
    }

    return role;
}

nlohmann::json AgentRoleLoader::ParseYamlFrontmatter(const std::string& yaml_str) const {
    nlohmann::json root = nlohmann::json::object();

    std::istringstream stream(yaml_str);
    std::string line;

    // 用于处理多行文本（| 语法）
    std::string current_key;
    std::string multiline_value;
    int multiline_indent = 0;

    while (std::getline(stream, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;

        int indent = 0;
        for (char c : line) {
            if (c == ' ')
                ++indent;
            else if (c == '\t')
                indent += 2;
            else
                break;
        }

        std::string trimmed = line.substr(line.find_first_not_of(" \t"));
        while (!trimmed.empty() &&
               (trimmed.back() == ' ' || trimmed.back() == '\t' ||
                trimmed.back() == '\r')) {
            trimmed.pop_back();
        }

        if (trimmed.empty()) continue;

        // 处理多行文本块
        if (!current_key.empty() && indent > multiline_indent) {
            multiline_value += trimmed + "\n";
            continue;
        } else if (!current_key.empty() && !multiline_value.empty()) {
            // 结束多行文本
            while (!multiline_value.empty() && multiline_value.back() == '\n') {
                multiline_value.pop_back();
            }
            root[current_key] = multiline_value;
            current_key.clear();
            multiline_value.clear();
        }

        if (trimmed[0] == '-') {
            std::string val = trimmed.substr(1);
            val.erase(0, val.find_first_not_of(" \t"));
            if (val.empty()) continue;

            if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                val = val.substr(1, val.size() - 2);
            }

            // 查找最后一个数组类型的 key 并添加元素
            bool found = false;
            for (auto it = root.rbegin(); it != root.rend(); ++it) {
                if (it->is_array()) {
                    it->push_back(val);
                    found = true;
                    LOG_DEBUG("Added '{}' to array for key '{}'", val, it.key());
                    break;
                }
            }

            // 如果没有找到数组，可能是缩进问题，跳过
            if (!found) {
                LOG_DEBUG("Orphan list item: {}, skipping", val);
            }
            continue;
        }

        size_t colon_pos = trimmed.find(':');
        if (colon_pos == std::string::npos) continue;

        std::string key = trimmed.substr(0, colon_pos);
        std::string value = trimmed.substr(colon_pos + 1);

        while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
            key.pop_back();
        value.erase(0, value.find_first_not_of(" \t"));
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
            value.pop_back();

        // 检查是否是多行文本块（| 语法）
        if (value == "|" || value == "|-") {
            current_key = key;
            multiline_indent = indent;
            multiline_value = "";
            continue;
        }

        if (value.empty()) {
            // 空值，可能是数组的开始
            // 初始化为空数组，等待后续 - 元素
            root[key] = nlohmann::json::array();
            LOG_DEBUG("Parsed key '{}' with empty value, initializing as array", key);
        } else if (value == "true") {
            root[key] = true;
        } else if (value == "false") {
            root[key] = false;
        } else {
            // 尝试解析为数字
            bool is_number = true;
            bool is_float = false;
            for (size_t i = 0; i < value.size(); ++i) {
                char c = value[i];
                if (c == '.') {
                    is_float = true;
                } else if (c == '-') {
                    if (i != 0) { is_number = false; break; }
                } else if (!std::isdigit(static_cast<unsigned char>(c))) {
                    is_number = false;
                    break;
                }
            }

            if (is_number) {
                if (is_float) {
                    root[key] = std::stod(value);
                } else {
                    root[key] = std::stoi(value);
                }
            } else {
                // 字符串，去除引号
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.size() - 2);
                }
                root[key] = value;
            }
        }
    }

    // 处理未完成的多行文本
    if (!current_key.empty() && !multiline_value.empty()) {
        while (!multiline_value.empty() && multiline_value.back() == '\n') {
            multiline_value.pop_back();
        }
        root[current_key] = multiline_value;
    }

    return root;
}

}  // namespace aicode
