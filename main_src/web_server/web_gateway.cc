// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "web_server/web_gateway.h"

#include "prosophor_core/public/agent_engine.h"
#include "common/log_wrapper.h"
#include "prosophor_core/public/config.h"
#include "prosophor_core/public/core_types.h"
#include "web_server/web_channel.h"

namespace prosophor {

WebGateway& WebGateway::GetInstance() {
    static WebGateway instance;
    return instance;
}

void WebGateway::Start() {
    if (running_.load()) return;

    // ── 创建 web 渠道(唯一渠道;每新增渠道 = 一个 ImChannel 子类 + 此处注册)──
    {
        auto channel = std::make_unique<WebChannel>();
        channel->SetInboundCallback(
            [this](ImMessage msg) { OnInboundMessage(std::move(msg)); });
        channels_by_name_[channel->GetName()] = channel.get();
        channels_.push_back(std::move(channel));
    }

    for (auto& channel : channels_) {
        if (!channel->Start()) {
            LOG_ERROR("WebGateway: channel '{}' failed to start", channel->GetName());
        }
    }

    // 统一输出回调:按 session 反向路由到渠道(多前端共存,必须 Add 不 Set)
    AgentEngine::GetInstance().AddOutputCallback(
        [this](const std::string& session_id, const std::string& role_id,
               AgentRuntimeState state, const std::string& state_msg,
               const std::optional<MessageSchema>& reply,
               const std::string& delta) {
            OnSessionOutput(session_id, role_id, state, state_msg, reply, delta);
        });

    running_.store(true);
    LOG_INFO("WebGateway started with {} channel(s)", channels_.size());
}

void WebGateway::Stop() {
    if (!running_.load()) return;
    running_.store(false);

    for (auto& channel : channels_) {
        channel->Stop();
    }
    channels_.clear();
    channels_by_name_.clear();

    {
        std::lock_guard<std::mutex> lock(chats_mutex_);
        chat_to_session_.clear();
        chats_.clear();
    }
}

void WebGateway::OnInboundMessage(ImMessage msg) {
    auto ctx = EnsureSessionForChat(msg);
    if (!ctx) {
        return;  // error already replied to the chat
    }

    LOG_INFO("WebGateway [{}]: chat={} session={} sender={} text='{}'", msg.channel,
             msg.chat_id, ctx->session_id, msg.sender_id, msg.text);
    AgentEngine::GetInstance().SendUserMessage(ctx->session_id, msg.text,
                                               msg.sender_id, msg.sender_name);
}

std::optional<ImChatContext> WebGateway::EnsureSessionForChat(ImMessage& msg) {
    // 多 bot 进同一群时以 bot 名隔离(渠道内部唯一)
    std::string map_key = msg.channel + "|" + msg.bot_name + "|" + msg.chat_id;

    std::string session_id;
    {
        std::lock_guard<std::mutex> lock(chats_mutex_);
        auto it = chat_to_session_.find(map_key);
        if (it != chat_to_session_.end()) {
            auto ctx_it = chats_.find(it->second);
            if (ctx_it != chats_.end()) {
                ctx_it->second.last_message_id = msg.message_id;
                return ctx_it->second;
            }
        }
    }

    // 归属主体:群聊 = chat_id(群),私聊 = chat_id(与用户一对一)
    SessionType session_type =
        (msg.chat_type == "group") ? SessionType::kGroup : SessionType::kDirect;
    std::string owner_id = msg.chat_id;

    try {
        session_id = AgentEngine::GetInstance().CreateSession(
            msg.role, msg.channel + " chat " + msg.chat_id, owner_id, session_type);
    } catch (const std::exception& e) {
        LOG_ERROR("WebGateway [{}]: CreateSession failed for chat {}: {}",
                  msg.channel, msg.chat_id, e.what());
        // 向渠道回发错误(FindChannel 失败则放弃)
        auto* channel = GetChannel(msg.channel);
        if (channel) {
            ImChatContext err_ctx;
            err_ctx.channel = msg.channel;
            err_ctx.chat_id = msg.chat_id;
            err_ctx.chat_type = msg.chat_type;
            err_ctx.last_message_id = msg.message_id;
            err_ctx.bot_name = msg.bot_name;
            channel->SendError(err_ctx,
                               "⚠ 服务不可用(角色未找到: " + msg.role + ")");
        }
        return std::nullopt;
    }

    ImChatContext ctx;
    ctx.channel = msg.channel;
    ctx.session_id = session_id;
    ctx.chat_id = msg.chat_id;
    ctx.chat_type = msg.chat_type;
    ctx.owner_id = owner_id;
    ctx.last_message_id = msg.message_id;
    ctx.bot_name = msg.bot_name;

    {
        std::lock_guard<std::mutex> lock(chats_mutex_);
        chat_to_session_[map_key] = session_id;
        chats_[session_id] = ctx;
    }
    LOG_INFO("WebGateway [{}]: created session {} for chat {}", msg.channel,
             session_id, msg.chat_id);
    return ctx;
}

void WebGateway::OnSessionOutput(const std::string& session_id,
                                const std::string& /*role_id*/,
                                AgentRuntimeState state,
                                const std::string& state_msg,
                                const std::optional<MessageSchema>& reply,
                                const std::string& delta) {
    if (!running_.load()) return;

    ImChatContext ctx;
    {
        std::lock_guard<std::mutex> lock(chats_mutex_);
        auto it = chats_.find(session_id);
        if (it == chats_.end()) return;
        ctx = it->second;
    }

    auto* channel = GetChannel(ctx.channel);
    if (!channel) return;

    // 流式帧:delta 直接透传(类型由 state 决定);终结帧:reply 为准
    std::string out_delta = delta;
    if (reply.has_value()) out_delta = reply->text();

    try {
        channel->OnAgentOutput(ctx, state, state_msg, out_delta, reply);
    } catch (const std::exception& e) {
        LOG_ERROR("WebGateway: channel '{}' OnAgentOutput failed: {}", ctx.channel, e.what());
    } catch (...) {
        LOG_ERROR("WebGateway: channel '{}' OnAgentOutput failed: unknown exception", ctx.channel);
    }

    // 每轮结束落盘(活跃会话消息在内存,Web 端刷新/重启需历史)
    if (state == AgentRuntimeState::COMPLETE ||
        state == AgentRuntimeState::STREAM_MODE_COMPLETE) {
        AgentEngine::GetInstance().FlushSession(session_id);
    }
}

void WebGateway::StopChat(const std::string& chat_id) {
    // 渠道默认单 web bot:map_key = "web||" + chat_id
    std::string map_key = "web||" + chat_id;
    std::string session_id;
    {
        std::lock_guard<std::mutex> lock(chats_mutex_);
        auto it = chat_to_session_.find(map_key);
        if (it != chat_to_session_.end()) {
            session_id = it->second;
        }
    }
    if (session_id.empty()) {
        return;
    }
    AgentEngine::GetInstance().StopSession(session_id);
}

std::optional<ImChatContext> WebGateway::GetChatContext(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(chats_mutex_);
    auto it = chats_.find(session_id);
    if (it == chats_.end()) return std::nullopt;
    return it->second;
}

std::vector<ImChatContext> WebGateway::ListChats() const {
    std::lock_guard<std::mutex> lock(chats_mutex_);
    std::vector<ImChatContext> result;
    result.reserve(chats_.size());
    for (const auto& [session_id, ctx] : chats_) {
        (void)session_id;
        result.push_back(ctx);
    }
    return result;
}

ImChannel* WebGateway::GetChannel(const std::string& name) {
    auto it = channels_by_name_.find(name);
    return it == channels_by_name_.end() ? nullptr : it->second;
}

}  // namespace prosophor
