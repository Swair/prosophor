// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "web_server/ws_handler.h"

#include <mutex>

#include <httplib.h>

#include "common/crypto_utils.h"
#include "common/log_wrapper.h"
#include "prosophor_core/public/config.h"
#include "web_server/web_gateway.h"
#include "web_server/web_channel.h"

namespace prosophor {

namespace {

/// send 回调与连接线程共享的 socket 状态;推送与销毁经 send_mutex 互斥。
struct WsSocketState {
    std::mutex send_mutex;
    bool closed = false;
    httplib::ws::WebSocket* ws = nullptr;
};

}  // namespace

WsHandler::WsHandler(WebChannel& channel, WebGateway& gateway, UserStore& users,
                     GroupStore& groups)
    : channel_(channel), gateway_(gateway), users_(users), groups_(groups) {}

void WsHandler::Register(httplib::Server& svr) {
    svr.WebSocket("/ws",
                  [this](const httplib::Request& req, httplib::ws::WebSocket& ws) {
                      OnConnection(req, ws);
                  });
}

void WsHandler::OnConnection(const httplib::Request& req, httplib::ws::WebSocket& ws) {
    // ── 认证:GET /ws?device_id=(免注册,自动建号)或 ?token=(既有会话)──
    std::string device_id = req.get_param_value("device_id");
    std::string fresh_token;
    std::optional<WebUser> user;
    if (!device_id.empty()) {
        const int ttl = ProsophorConfig::GetInstance().web.token_ttl_hours;
        auto result = users_.GetOrCreateByDeviceId(device_id, ttl);
        if (!result.ok) {
            ws.send(MakeErrorFrame("unauthorized", "invalid device_id"));
            ws.close(httplib::ws::CloseStatus::PolicyViolation, "unauthorized");
            return;
        }
        user = users_.GetUser(result.user_id);
        fresh_token = result.value;
    } else {
        user = users_.ValidateToken(req.get_param_value("token"));
    }
    if (!user) {
        ws.send(MakeErrorFrame("unauthorized", "invalid or missing identity"));
        ws.close(httplib::ws::CloseStatus::PolicyViolation, "unauthorized");
        return;
    }
    const std::string user_id = user->user_id;
    const std::string display_name = user->display_name;

    const std::string conn_id = GenerateUuid();
    auto state = std::make_shared<WsSocketState>();
    state->ws = &ws;

    channel_.AttachConnection(
        conn_id, user_id, display_name,
        [state](const std::string& message) -> bool {
            std::lock_guard<std::mutex> lock(state->send_mutex);
            if (state->closed || !state->ws) return false;
            return state->ws->send(message);
        });

    // 认证成功通知(device 免注册时携带新 token,前端持久化)
    if (!ws.send(MakeWelcomeFrame(user_id, user->username, display_name,
                                  CurrentTimestampMs(), fresh_token))) {
        channel_.DetachConnection(conn_id);
        return;
    }

    LOG_INFO("WsHandler: connection '{}' opened (user={}, remote={})", conn_id, user_id,
             req.remote_addr);

    // 上线 presence:用户所在所有群广播
    BroadcastUserPresence(user_id, true);

    // ── 帧收发循环(read 内部处理 ping/pong/close)──
    std::string message;
    while (true) {
        auto result = ws.read(message);
        if (result == httplib::ws::ReadResult::Fail) {
            break;
        }
        HandleFrame(conn_id, user_id, display_name, ws, message);
        message.clear();
    }

    // ── 关闭清理:先置 closed(与推送互斥),再摘除连接,最后广播离线 ──
    {
        std::lock_guard<std::mutex> lock(state->send_mutex);
        state->closed = true;
    }
    channel_.DetachConnection(conn_id);
    BroadcastUserPresence(user_id, false);
    LOG_INFO("WsHandler: connection '{}' closed", conn_id);
}

void WsHandler::BroadcastUserPresence(const std::string& user_id, bool online) {
    for (const auto& group : groups_.ListGroupsForUser(user_id)) {
        channel_.BroadcastPresence("group_" + group.group_id, user_id, online);
    }
}

void WsHandler::HandleFrame(const std::string& conn_id, const std::string& user_id,
                            const std::string& display_name, httplib::ws::WebSocket& ws,
                            const std::string& message) {
    auto frame = ParseClientFrame(message);
    if (!frame) {
        ws.send(MakeErrorFrame("bad_request", "invalid frame", ""));
        return;
    }

    if (frame->send) {
        channel_.InboundSend(conn_id, *frame->send);
    } else if (frame->stop) {
        gateway_.StopChat(frame->stop->chat_id);
    } else if (frame->ping) {
        ws.send(MakePongFrame(frame->ping->t));
    } else if (frame->get_online) {
        const std::string& chat_id = frame->get_online->chat_id;
        if (WebChannel::IsGroupChat(chat_id)) {
            // 群聊:成员列表 + 在线者
            std::string gid = chat_id.substr(std::string("group_").size());
            std::vector<std::pair<std::string, std::string>> members;
            for (const auto& uid : groups_.MemberIds(gid)) {
                auto member = users_.GetUser(uid);
                members.emplace_back(uid, member ? member->display_name : uid);
            }
            ws.send(MakeOnlineFrame(chat_id, members, channel_.OnlineUserIdsForChat(chat_id)));
        } else {
            // 私聊:自己
            ws.send(MakeOnlineFrame(chat_id, {{user_id, display_name}},
                                    channel_.OnlineUserIdsForChat(chat_id)));
        }
    } else if (frame->hello) {
        // 已在握手认证;hello 帧仅作再认证占位(未来支持 token 刷新)
        (void)user_id;
    }
}

}  // namespace prosophor
