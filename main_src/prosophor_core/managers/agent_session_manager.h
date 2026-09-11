// Copyright 2026 Prosophor Contributors
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

namespace prosophor {

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
    /// @param owner_id IM 归属主体(用户/群 ID),空表示本机单用户模式
    /// @param session_type 会话类型:direct(单聊)/group(群聊),决定记忆注入范围
    std::string CreateSession(const std::string& role_id,
                              const std::string& task_desc = "",
                              const std::string& owner_id = "",
                              SessionType session_type = SessionType::kDirect);

    /// 向指定会话发送消息（同步）
    std::string SendToSession(const std::string& session_id,
                              const std::string& message,
                              const std::string& sender_id = "",
                              const std::string& sender_name = "");

    /// 向指定会话发送消息（同步，随附图片）
    /// image_paths 在本调用内、持会话锁设置给瞬时附件位 —— 与 Loop 取图同锁，
    /// 不留给调用方先设后发的竞态窗口
    std::string SendToSession(const std::string& session_id,
                              const std::string& message,
                              const std::vector<std::string>& image_paths,
                              const std::string& sender_id = "",
                              const std::string& sender_name = "");

    /// 向指定会话发送消息（异步，不阻塞）
    void SendToSessionAsync(const std::string& session_id,
                            const std::string& message,
                            const std::string& sender_id = "",
                            const std::string& sender_name = "");

    /// 获取会话
    AgentSession* GetSession(const std::string& session_id);
    const AgentSession* GetSession(const std::string& session_id) const;

    /// 获取会话的 shared_ptr（线程安全，防止悬空指针）
    std::shared_ptr<AgentSession> GetSessionShared(const std::string& session_id);

    /// 按角色筛选会话
    std::vector<AgentSession*> GetSessionsByRole(const std::string& role_id);
    std::vector<const AgentSession*> GetSessionsByRole(const std::string& role_id) const;

    /// 重建某角色的所有活跃会话的 system prompt（HotReload 后调用）
    void RebuildSystemPromptForRole(const std::string& role_id);

    /// 获取活跃会话（最近 N 分钟）
    std::vector<AgentSession*> GetActiveSessions(int minutes = 30);

    /// 关闭会话
    void CloseSession(const std::string& session_id);

    /// 将未落盘消息刷入持久化日志(不关闭会话;Web 端每轮回复后调用)
    void FlushSession(const std::string& session_id);

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
    /// - 如果有相关活跃会话(同 role + 同 owner + 同类型)→ 复用
    /// - 否则 → 创建新会话
    std::string GetOrCreateSession(const std::string& role_id,
                                   const std::string& message_hint,
                                   const std::string& owner_id = "",
                                   SessionType session_type = SessionType::kDirect);

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

    void Initialize(ToolExecutorCallback tool_executor);

    /// 设置工具执行器
    void SetToolExecutor(ToolExecutorCallback tool_executor);

    /// 设置输出回调（清空已有回调后添加——兼容旧调用，单前端语义）
    void SetOutputCallback(SessionOutputCallback callback);
    /// 追加输出回调（多前端共存：SDL Sprite + Web 网关可同时注册）
    void AddOutputCallback(SessionOutputCallback callback);
    /// 清空全部输出回调
    void ClearOutputCallbacks();

private:
    AgentSessionManager() = default;

    std::unordered_map<std::string, AgentRole> roles_;
    std::unordered_map<std::string, std::unique_ptr<AgentSession>> sessions_;

    ToolExecutorCallback tool_executor_;

    /// 多前端输出回调注册表（session 通过扇出 lambda 遍历）
    std::vector<SessionOutputCallback> output_callbacks_;
    mutable std::mutex output_mutex_;

    mutable std::mutex mutex_;

    // ── Pending buffer（连续输入合并）─────────────────────
    /// 一条待处理输入:text + 发送者(群聊多成员连发时逐条 Loop)
    struct PendingInput {
        std::string text;
        std::string sender_id;
        std::string sender_name;
    };
    std::unordered_map<std::string, std::vector<PendingInput>> pending_inputs_;
    std::unordered_map<std::string, bool> task_active_;
    std::mutex pending_mutex_;

    /// 生成唯一 session ID
    std::string GenerateSessionId(const std::string& role_id);

    /// 构建 system prompt（组合 Role Memory + Session History + Role 配置）
    std::vector<SystemSchema> BuildSystemPrompt(const AgentSession& session);

    /// 取一条消息提交线程池，在单任务内内联循环处理后续积压
    void StartChain(const std::string& session_id,
                    std::shared_ptr<AgentSession> session);
};

}  // namespace prosophor
