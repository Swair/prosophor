// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <optional>
#include <functional>
#include <atomic>

#include <nlohmann/json.hpp>

#include "core/agent_role.h"
#include "common/messages_schema.h"
#include "providers/llm_provider.h"
#include "providers/provider_router.h"
#include "common/time_wrapper.h"
#include "components/ui_types.h"  // For AgentRuntimeState

// Forward declaration for MemoryConsolidationService
namespace prosophor { class MemoryConsolidationService; }

namespace prosophor {

/// Tool executor callback
using ToolExecutorCallback = std::function<std::string(const std::string& tool_name, const nlohmann::json& args)>;

/// Session output callback - notifies UI of state changes and message output
/// Parameters: session_id, role_id, state, state_msg, reply
using SessionOutputCallback = std::function<void(const std::string& session_id,
                                                  const std::string& role_id,
                                                  AgentRuntimeState state,
                                                  const std::string& state_msg,
                                                  const std::optional<MessageSchema>& reply)>;

/// Agent 会话实例（运行时）
/// 记录"这个精灵在做什么任务"
///
/// 记忆双层设计：
/// - Role Memory (长期记忆): 角色专属习惯、偏好，跨项目复用 (~/.prosophor/memories/{role_id}/)
/// - Session History (会话历史): 当前项目的对话历史、上下文、决策 (~/.prosophor/sessions/{session_id}/history/)
struct AgentSession {
    // =====================
    // 输入（AgentCore 的输入参数）
    // =====================

    // --- 角色配置 ---
    AgentRole* role = nullptr;               // 角色指针（初始指向共享定义，切换后指向 mutable_role_）
    std::optional<AgentRole> mutable_role_; // 运行时可修改的 role 副本（切换 provider/model 时创建）

    // --- 运行时配置 ---
    bool use_tools = true;                  // 是否使用工具
    std::string working_directory;          // 工作目录
    ToolExecutorCallback tool_executor;     // 工具执行器
    SessionOutputCallback output_callback;  // 输出回调（通知 UI 状态和消息）

    // --- 运行时依赖（由外部注入）---
    MemoryConsolidationService* consolidation_service = nullptr;  // 记忆沉淀服务（弱引用）

    // --- 上下文 ---
    std::vector<SystemSchema> system_prompt; // System prompt
    std::vector<MessageSchema> messages;     // 对话历史（含用户输入）
    std::vector<std::string> related_files;  // 相关文件

    // =====================
    // 输出（AgentCore 的运行结果）
    // =====================

    // --- 对话输出 ---
    // messages 字段同时也是输出（追加 assistant 回复）

    // --- 运行状态 ---
    AgentRuntimeState state = AgentRuntimeState::IDLE;  // 当前运行状态
    std::string state_message;                           // 状态描述

    // --- 控制标志 ---
    std::atomic<bool> stop_requested{false};  // 中断标志（用户可设置）

    // =====================
    // 元数据（不属于输入/输出）
    // =====================

    // --- 身份 ---
    std::string session_id;            // 唯一 ID: "coder-abc123"
    std::string role_id;               // 关联角色："coder"
    std::string task_description;      // 任务描述："写排序"

    // --- Provider 实例（运行时依赖）---
    std::shared_ptr<LLMProvider> provider;  // Provider 实例
    std::string base_url;                   // Agent base_url（来自 config agents 配置）
    std::string api_key;                    // Agent api_key（来自 config agents 配置）
    int timeout = 60;                       // Agent timeout（来自 config agents 配置）

    // --- 记忆管理 ---
    std::string session_history_dir;   // Session 会话历史目录

    // --- 生命周期 ---
    SteadyClock::TimePoint created_at;
    SteadyClock::TimePoint last_active;
    bool is_active = true;

    // === 构造函数 ===
    AgentSession() {
        created_at = SteadyClock::Now();
        last_active = SteadyClock::Now();
    }

    AgentSession(const std::string& sid, const std::string& rid,
                 const std::string& task, AgentRole* r)
        : role(r), session_id(sid), role_id(rid), task_description(task) {
        created_at = SteadyClock::Now();
        last_active = SteadyClock::Now();

        // 初始化运行时状态（从 role 复制配置）
        if (r) {
            provider = ProviderRouter::GetInstance().GetProviderByName(r->provider_prot);
            use_tools = true;
            working_directory.clear();
            messages.clear();
            system_prompt.clear();
            session_history_dir.clear();
        }
    }

    // 显式定义移动操作（因为 std::atomic 和 std::function 需要特殊处理）
    // 按字段分类顺序：输入 -> 输出 -> 元数据
    AgentSession(AgentSession&& other) noexcept
        : role(other.role),
          mutable_role_(std::move(other.mutable_role_)),
          use_tools(other.use_tools),
          working_directory(std::move(other.working_directory)),
          tool_executor(std::move(other.tool_executor)),
          output_callback(std::move(other.output_callback)),
          consolidation_service(other.consolidation_service),  // 浅拷贝指针
          system_prompt(std::move(other.system_prompt)),
          messages(std::move(other.messages)),
          related_files(std::move(other.related_files)),
          state(other.state),
          state_message(std::move(other.state_message)),
          stop_requested(other.stop_requested.load()),
          session_id(std::move(other.session_id)),
          role_id(std::move(other.role_id)),
          task_description(std::move(other.task_description)),
          provider(std::move(other.provider)),
          base_url(std::move(other.base_url)),
          api_key(std::move(other.api_key)),
          timeout(other.timeout),
          session_history_dir(std::move(other.session_history_dir)),
          created_at(other.created_at),
          last_active(other.last_active),
          is_active(other.is_active) {
    }

    AgentSession& operator=(AgentSession&& other) noexcept {
        if (this != &other) {
            // 输入
            role = other.role;
            mutable_role_ = std::move(other.mutable_role_);
            use_tools = other.use_tools;
            working_directory = std::move(other.working_directory);
            tool_executor = std::move(other.tool_executor);
            output_callback = std::move(other.output_callback);
            consolidation_service = other.consolidation_service;  // 浅拷贝指针
            system_prompt = std::move(other.system_prompt);
            messages = std::move(other.messages);
            related_files = std::move(other.related_files);
            // 输出
            state = other.state;
            state_message = std::move(other.state_message);
            stop_requested.store(other.stop_requested.load());
            // 元数据
            session_id = std::move(other.session_id);
            role_id = std::move(other.role_id);
            task_description = std::move(other.task_description);
            provider = std::move(other.provider);
            base_url = std::move(other.base_url);
            api_key = std::move(other.api_key);
            timeout = other.timeout;
            session_history_dir = std::move(other.session_history_dir);
            created_at = other.created_at;
            last_active = other.last_active;
            is_active = other.is_active;
        }
        return *this;
    }

    AgentSession(const AgentSession&) = delete;
    AgentSession& operator=(const AgentSession&) = delete;

    /// 应用 provider 覆盖（手动切换的优先级高于 role 配置）
    /// 创建 role 的本地副本，role 指针指向它。BuildRequest 无需任何 override 逻辑。
    void ApplyProviderOverride(const std::string& provider_name, const std::string& model = "") {
        auto& router = ProviderRouter::GetInstance();
        provider = router.GetProviderByName(provider_name);

        // 创建 role 本地副本
        mutable_role_ = *role;
        role = &mutable_role_.value();
        role->provider_prot = provider_name;

        auto& config = ProsophorConfig::GetInstance();
        auto prov_it = config.providers.find(provider_name);
        if (prov_it != config.providers.end()) {
            auto& agent_map = prov_it->second.agents;
            const AgentConfig* matched = nullptr;

            // Set provider-level defaults first
            base_url = prov_it->second.base_url;
            api_key = prov_it->second.api_key;
            timeout = prov_it->second.timeout;

            // Try 1: model as agent key
            auto agent_it = agent_map.find(model);
            if (agent_it != agent_map.end()) {
                matched = &agent_it->second;
            } else if (!model.empty()) {
                // Try 2: model as model name (search all agents)
                for (auto& [k, v] : agent_map) {
                    if (v.model == model) {
                        matched = &v;
                        break;
                    }
                }
            }

            if (matched) {
                role->model = matched->model;
                // Find the correct entry's base_url/api_key/timeout by searching all entries
                std::string entry_base_url;
                std::string entry_api_key;
                int entry_timeout = 0;
                if (prov_it->second.FindEntryForModel(provider_name, model, entry_base_url,
                                                       entry_api_key, entry_timeout)) {
                    base_url = entry_base_url;
                    api_key = entry_api_key;
                    timeout = entry_timeout;
                }
                role->temperature = matched->temperature;
                role->max_tokens = matched->max_tokens;
                role->thinking = matched->thinking;
                LOG_DEBUG("Applied provider override: provider={}, model={}, base_url={}",
                         provider_name, matched->model, base_url);
            } else {
                LOG_WARN("No matching agent for '{}' in provider '{}', using provider defaults",
                         model, provider_name);
            }
        }
    }
};

}  // namespace prosophor
