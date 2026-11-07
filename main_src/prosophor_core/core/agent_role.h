// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <optional>
#include <sstream>
#include <memory>

#include <nlohmann/json.hpp>

#include "common/file_utils.h"  // For ReadFile

#include "tools/tool_registry.h"  // For ToolsSchema
#include "core/dialog_strategy.h"
#include "core/memory_manager.h"

namespace prosophor {

/// Agent 角色定义（模板/蓝图）
/// 定义"是什么精灵"：性格、技能、记忆、指令
struct AgentRole {
    // === 基础身份 ===
    std::string id;                    // "coder", "reviewer", "architect"
    std::string name;                  // "代码专家"
    std::string description;           // 角色描述
    std::string sprite_id;           // 指向 petdex-sprites 的精灵 id（SDL 模式用，TUI 忽略）
    std::string sprite_assets_dir;   // 精灵资源目录（非空时优先于 sprite_id 的 petdex 查找）
    std::string tts_backend;
    std::string tts_voice;
    bool tts_auto_start = false;

    // === Provider 配置（角色可绑定专属 Provider）===
    std::string provider_prot;          // ""=使用全局默认，或指定"anthropic"/"ollama"/"deepseek"
    std::string model;                 // 专属模型
    double temperature = 0.7;
    int max_tokens = 8192;
    int context_window = 128000;
    bool enable_streaming = true;      // 是否启用流式输出
    // AgentChat：单轮带工具对话（不是 ReAct 多轮）。一次 LLM 响应里的
    // tool_calls 执行完即结束，结果不回灌；同轮文字直接作为最终回复。
    bool enable_agent_chat = false;
    bool enable_summary = false;       // 是否启用对话摘要（system prompt 指令 + 摘要提取循环）
    bool thinking = false;
    int thinking_budget_tokens = 4096;
    std::string reasoning_effort = "medium";

    // === 性格配置 ===
    std::string personality;           // "concise", "detailed", "cautious", "creative"
    std::string soul;    // 性格提示词："你说话简洁，直接给代码"

    // === 系统指令 ===
    std::string role_system_prompt;         // 角色专属 system prompt

    // === 能力配置 ===
    std::vector<std::string> skills;           // 启用的技能列表
    std::vector<ToolsSchema> tools;            // 工具列表（运行时从 ToolRegistry 加载）

    // === 行为约束 ===
    int max_iterations = 15;           // 最大工具调用轮次
    bool enable_tools = true;          // 是否启用工具
    bool auto_confirm_tools = false;   // 是否自动确认工具

    // === 记忆配置 ===
    std::string memory_dir;            // 专属记忆目录：~/.prosophor/memories/coder

    // === 对话策略（共享，同策略的 role 索引同一实例） ===
    std::shared_ptr<DialogStrategy> dialog_strategy;

    // === 记忆策略 ===
    std::shared_ptr<MemoryStrategy> memory_strategy;

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
        if (!soul.empty()) {
            if (!prompt.empty()) {
                prompt += "\n";
            }
            prompt += "性格要求：" + soul + "\n";
        }

        return prompt;
    }

    /// 加载 Role Memory 文件内容（从 memory_dir 读取 .md 文件）
    std::string LoadMemoryContent() const {
        std::ostringstream content;

        if (memory_dir.empty() || !DirExists(memory_dir)) {
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
