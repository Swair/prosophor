// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "prosophor_core/public/core_types.h"

namespace prosophor {

// ============================================================================
// 统一 IM 渠道消息模型
// 新增渠道(微信/钉钉/Telegram...) = 实现 ImChannel 子类 + 配置节,
// 核心层与 WebGateway 不感知具体协议。
// ============================================================================

/// 入站消息(渠道解析后交给 WebGateway 统一路由)
struct ImMessage {
    std::string channel;      // 渠道名:"web" | ...
    std::string role;         // 该渠道/账号绑定的 AgentRole(渠道配置提供)
    std::string bot_name;     // 渠道内账号名(飞书 bot 名),用于同群多账号隔离
    std::string chat_id;      // 会话标识(飞书 oc_xxx)
    std::string chat_type;    // "p2p" | "group"
    std::string sender_id;    // 发言的群成员用户 ID(群聊必填;飞书 ou_xxx),不是群 ID
    std::string sender_name;  // 发言成员展示名,可空
    std::string text;         // 去 @ 后的消息文本
    std::string message_id;   // 回引用(回复挂载),可空
};

/// 会话级上下文(WebGateway 维护,渠道回发时使用)
struct ImChatContext {
    std::string channel;
    std::string session_id;
    std::string chat_id;
    std::string chat_type;
    std::string owner_id;      // 净化后的归属主体:私聊=用户,群聊=群
    std::string last_message_id;
    std::string bot_name;      // 渠道内账号名(飞书 bot 名),可空
};

/// IM 渠道抽象基类。
/// 生命周期:WebGateway::Start() 创建并调用 Start()/Stop()。
/// 入站:子类收到消息后构造 ImMessage 调 DispatchInbound()。
/// 出站:WebGateway 的输出回调按 session 找到 ctx 后调 OnAgentOutput()。
class ImChannel {
public:
    virtual ~ImChannel() = default;

    virtual const std::string& GetName() const = 0;
    virtual bool Start() = 0;
    virtual void Stop() = 0;

    /// 核心输出回调转发(状态 + 状态说明 + 增量 + 终答)。
    /// 在全局线程池线程上调用,渠道内部不得阻塞,只入队/攒状态。
    virtual void OnAgentOutput(const ImChatContext& ctx, AgentRuntimeState state,
                               const std::string& state_msg,
                               const std::string& delta,
                               const std::optional<MessageSchema>& reply) = 0;

    /// 立即发送错误文本(会话创建失败等,不走输出回调)。
    virtual void SendError(const ImChatContext& ctx, const std::string& error) = 0;

    /// 由 WebGateway 注入:收到消息时路由进核心会话。
    void SetInboundCallback(std::function<void(ImMessage)> cb) {
        inbound_cb_ = std::move(cb);
    }

protected:
    /// 子类收到消息时调用(线程安全)。
    void DispatchInbound(ImMessage msg) {
        if (inbound_cb_) inbound_cb_(std::move(msg));
    }

private:
    std::function<void(ImMessage)> inbound_cb_;
};

}  // namespace prosophor
