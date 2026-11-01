// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/noncopyable.h"
#include "prosophor_core/public/core_types.h"
#include "web_server/channel.h"

namespace prosophor {

/// Web 消息网关:维护 chat → AgentSession 映射,统一把核心输出路由回渠道。
/// 渠道(当前唯一:WebChannel)只负责传输(协议解析/发送),不感知会话与记忆。
/// 未来新增渠道 = 实现 ImChannel 子类 + 在此注册。
class WebGateway : public Noncopyable {
public:
    static WebGateway& GetInstance();

    /// 创建 WebChannel(web.enabled 时)并注册 AgentEngine 输出回调。
    /// Idempotent;由 main_web.cc 在 AgentEngine 初始化后调用。
    void Start();

    /// 停止全部渠道并移除回调。
    void Stop();

    bool running() const { return running_.load(); }

    /// 渠道消息统一入口:找/建会话 → 转发核心。
    void OnInboundMessage(ImMessage msg);

    /// 停止某 chat 的生成(web 端 stop 帧;chat_id 含渠道前缀)。
    void StopChat(const std::string& chat_id);

    /// 按 session_id 查会话上下文(REST 层访问控制/历史读取用)。
    std::optional<ImChatContext> GetChatContext(const std::string& session_id) const;

    /// 全部会话上下文快照(REST 层按用户可见性过滤)。
    std::vector<ImChatContext> ListChats() const;

    /// 按名取渠道(web_server 层取 WebChannel 用);未找到返回 nullptr。
    ImChannel* GetChannel(const std::string& name);

private:
    WebGateway() = default;

    /// 找/建 chat 对应的 AgentSession;填充 ImChatContext。
    /// 失败时向渠道回发错误并返回空 ctx。
    std::optional<ImChatContext> EnsureSessionForChat(ImMessage& msg);

    /// 输出回调(全局线程池线程):按 session 查 ctx → 转发渠道 OnAgentOutput。
    void OnSessionOutput(const std::string& session_id, const std::string& role_id,
                         AgentRuntimeState state, const std::string& state_msg,
                         const std::optional<MessageSchema>& reply, const std::string& delta);

    // ── state ───────────────────────────────────────────────────────────
    std::atomic<bool> running_{false};

    std::vector<std::unique_ptr<ImChannel>> channels_;
    std::unordered_map<std::string, ImChannel*> channels_by_name_;

    mutable std::mutex chats_mutex_;
    std::unordered_map<std::string, std::string> chat_to_session_;  // channel|chat_id -> session_id
    std::unordered_map<std::string, ImChatContext> chats_;          // session_id -> ctx
};

}  // namespace prosophor
