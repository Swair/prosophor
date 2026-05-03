// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <optional>
#include <sstream>

#include <nlohmann/json.hpp>

#include "common/file_utils.h"  // For ReadFile

#include "tools/tool_registry.h"  // For ToolsSchema

namespace prosophor {

/// Agent 角色定义（模板/蓝图）
/// 定义"是什么精灵"：性格、技能、记忆、指令
struct AgentRole {
    // === 基础身份 ===
    std::string id;                    // "coder", "reviewer", "architect"
    std::string name;                  // "代码专家"
    std::string description;           // 角色描述
    std::string avatar;                // 头像/emoji，如 "👨‍💻"

    // === Provider 配置（角色可绑定专属 Provider）===
    std::string provider_prot;          // ""=使用全局默认，或指定"anthropic"/"ollama"/"deepseek"
    std::string model;                 // 专属模型
    double temperature = 0.7;
    int max_tokens = 8192;
    bool enable_streaming = true;      // 是否启用流式输出
    bool thinking = false;

    // === 性格配置 ===
    std::string personality;           // "concise", "detailed", "cautious", "creative"
    std::string personality_prompt;    // 性格提示词："你说话简洁，直接给代码"

    // === 系统指令 ===
    std::string role_system_prompt;         // 角色专属 system prompt

    // === 能力配置 ===
    std::vector<std::string> skills;           // 启用的技能列表
    std::vector<ToolsSchema> tools;            // 工具列表（运行时从 ToolRegistry 加载）

    // === 行为约束 ===
    int max_iterations = 15;           // 最大工具调用轮次
    bool auto_confirm_tools = false;   // 是否自动确认工具

    // === 记忆配置 ===
    std::string memory_dir;            // 专属记忆目录：~/.prosophor/memories/coder

    /// 检查是否绑定了专属 Provider
    bool HasCustomProvider() const {
        return !provider_prot.empty();
    }

    /// 生成角色专属 prompt（包含 system prompt 和 personality）
    std::string BuildPrompt() const {
        std::string prompt;

        // 1. 角色基础 System Prompt
        if (!role_system_prompt.empty()) {
            prompt += role_system_prompt + "\n";
        }

        // 2. 性格要求
        if (!personality_prompt.empty()) {
            if (!prompt.empty()) {
                prompt += "\n";
            }
            prompt += "性格要求：" + personality_prompt + "\n";
        }

        return prompt;
    }

    /// 加载 Role Memory 文件内容（从 memory_dir 读取 .md 文件）
    std::string LoadMemoryContent() const {
        std::ostringstream content;

        if (memory_dir.empty() || !std::filesystem::exists(memory_dir)) {
            return "";
        }

        content << "## 角色习惯与偏好\n\n";

        for (const auto& entry : std::filesystem::directory_iterator(memory_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".md") {
                auto file_content = ReadFile(entry.path().string());
                if (file_content.has_value()) {
                    content << "### " << entry.path().stem().string() << "\n";
                    content << file_content.value() << "\n\n";
                }
            }
        }

        return content.str();
    }
};

}  // namespace prosophor
