// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <functional>
#include <atomic>

#include <nlohmann/json.hpp>

#include "common/thread_pool.h"
#include "core/agent_role.h"
#include "core/agent_session.h"
#include "core/agent_core.h"
#include "managers/memory_manager.h"

namespace aicode {

/// AgentSessionManager: 管理多 Agent 角色和多会话
/// 支持：
/// 1. 不同角色同时对话（coder + reviewer + architect）
/// 2. 同一角色多任务并行（coder-task-1, coder-task-2）
class AgentSessionManager {
public:
    static AgentSessionManager& GetInstance();

    // =====================
    // 角色管理（AgentRole）
    // =====================

    /// 注册一个角色
    void RegisterRole(const AgentRole& role);

    /// 从目录加载所有角色配置
    void LoadRolesFromDirectory(const std::string& roles_dir);

    /// 获取角色定义
    const AgentRole* GetRole(const std::string& role_id) const;

    /// 列出所有角色
    std::vector<std::string> ListRoles() const;

    // =====================
    // 会话管理（AgentSession）
    // =====================

    /// 创建新会话（同一角色可创建多个）
    std::string CreateSession(const std::string& role_id,
                              const std::string& task_desc = "");

    /// 向指定会话发送消息（同步）
    std::string SendToSession(const std::string& session_id,
                              const std::string& message);

    /// 向指定会话发送消息（异步，不阻塞）
    void SendToSessionAsync(const std::string& session_id,
                            const std::string& message);

    /// 获取会话
    AgentSession* GetSession(const std::string& session_id);
    const AgentSession* GetSession(const std::string& session_id) const;

    /// 获取会话的 shared_ptr（线程安全，防止悬空指针）
    std::shared_ptr<AgentSession> GetSessionShared(const std::string& session_id);

    /// 按角色筛选会话
    std::vector<AgentSession*> GetSessionsByRole(const std::string& role_id);
    std::vector<const AgentSession*> GetSessionsByRole(const std::string& role_id) const;

    /// 获取活跃会话（最近 N 分钟）
    std::vector<AgentSession*> GetActiveSessions(int minutes = 30);

    /// 关闭会话
    void CloseSession(const std::string& session_id);

    /// 列出所有会话
    std::vector<std::string> ListSessions() const;

    /// 获取最后一个会话 ID
    std::string GetLastSessionId() const;

    /// 切换会话的角色（保持 Session History 连续）
    void SwitchRoleForSession(const std::string& session_id, const std::string& new_role_id);

    // =====================
    // 智能路由
    // =====================

    /// 自动找到或创建会话
    /// - 如果有相关活跃会话 → 复用
    /// - 否则 → 创建新会话
    std::string GetOrCreateSession(const std::string& role_id,
                                   const std::string& message_hint);

    // =====================
    // 群聊/广播
    // =====================

    /// 向多个会话发送消息（异步，不等待结果）
    /// 结果通过 output_callback 通知
    void BroadcastToSessions(const std::vector<std::string>& session_ids,
                             const std::string& message);

    /// 向某角色的所有活跃会话广播（异步，不等待结果）
    /// 结果通过 output_callback 通知
    void BroadcastToRole(const std::string& role_id,
                         const std::string& message);

    // =====================
    // 初始化
    // =====================

    void Initialize(std::shared_ptr<MemoryManager> memory_manager,
                    ToolExecutorCallback tool_executor);

    /// 设置工具执行器
    void SetToolExecutor(ToolExecutorCallback tool_executor);

    /// 设置输出回调
    void SetOutputCallback(SessionOutputCallback callback);

private:
    AgentSessionManager() = default;

    std::unordered_map<std::string, AgentRole> roles_;
    std::unordered_map<std::string, AgentSession> sessions_;

    std::shared_ptr<MemoryManager> memory_manager_;
    ToolExecutorCallback tool_executor_;
    SessionOutputCallback output_callback_;

    mutable std::mutex mutex_;

    /// 生成唯一 session ID
    std::string GenerateSessionId(const std::string& role_id);

    /// 切换记忆上下文
    void SwitchMemoryContext(const AgentSession& session);

    /// 构建 system prompt（组合 Role Memory + Session History + Role 配置）
    std::vector<SystemSchema> BuildSystemPrompt(const AgentSession& session);

    ThreadPool thread_pool_;
};

}  // namespace aicode
