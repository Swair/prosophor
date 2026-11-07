// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>

#include "prosophor_core/public/core_types.h"

namespace prosophor {

// ============================================================================
// Web 渠道消息协议:JSON text 帧,纯函数编解码(可 gtest,不依赖网络)。
// 客户端 → 服务端:hello / send / stop / ping / get_online
// 服务端 → 客户端:welcome / ack / session_ready / stream_start / delta /
//                 state / reply / error / pong / online / presence
// ============================================================================

// ── 客户端 → 服务端 ────────────────────────────────────────────────────────
struct ClientHello { std::string token; std::string client; };
struct ClientSend { std::string chat_id; std::string text; std::string client_msg_id; };
struct ClientStop { std::string chat_id; };
struct ClientPing { std::string t; };
struct ClientGetOnline { std::string chat_id; };

/// 客户端帧(任一时刻只有一个子字段非空)
struct ClientFrame {
    std::string type;                            // "hello" | "send" | "stop" | "ping" | "get_online"
    std::optional<ClientHello> hello;
    std::optional<ClientSend> send;
    std::optional<ClientStop> stop;
    std::optional<ClientPing> ping;
    std::optional<ClientGetOnline> get_online;
};

/// 解析客户端 JSON 帧;非法 JSON / 未知 type 返回 nullopt。
std::optional<ClientFrame> ParseClientFrame(const std::string& json_text);

// ── 服务端 → 客户端 ────────────────────────────────────────────────────────
/// device 免注册时 welcome 帧携带新签发的 token(空则不携带)。
std::string MakeWelcomeFrame(const std::string& user_id, const std::string& username,
                             const std::string& display_name, int64_t server_ts,
                             const std::string& token = "");
std::string MakeAckFrame(const std::string& client_msg_id, const std::string& chat_id,
                         const std::string& session_id, int64_t ts);
std::string MakeSessionReadyFrame(const std::string& chat_id, const std::string& session_id,
                                  const std::string& role_id, bool created);
std::string MakeStreamStartFrame(const std::string& chat_id, const std::string& session_id,
                                 const std::string& role_id);
std::string MakeDeltaFrame(const std::string& chat_id, const std::string& session_id,
                           const std::string& delta);
std::string MakeStateFrame(const std::string& chat_id, const std::string& session_id,
                           const std::string& state, const std::string& state_msg);
std::string MakeReplyFrame(const std::string& chat_id, const std::string& session_id,
                           const std::string& content, int64_t ts);
std::string MakeErrorFrame(const std::string& code, const std::string& message,
                           const std::string& chat_id = "");
std::string MakePongFrame(const std::string& t);
std::string MakeOnlineFrame(const std::string& chat_id,
                            const std::vector<std::pair<std::string, std::string>>& members,
                            const std::vector<std::string>& online_ids);
std::string MakePresenceFrame(const std::string& chat_id, const std::string& user_id,
                              bool online);

/// AgentRuntimeState → 前端语义字符串:
/// idle / working / streaming / thinking / waiting_permission / error
std::string StateToString(AgentRuntimeState state);

/// 当前 Unix 时间戳(毫秒)。
int64_t CurrentTimestampMs();

}  // namespace prosophor
