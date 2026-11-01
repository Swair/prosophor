// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "web_server/web_channel.h"

#include <algorithm>

#include "common/log_wrapper.h"
#include "prosophor_core/public/config.h"

namespace prosophor {

namespace {
constexpr const char* kWebChannelName = "web";
constexpr const char* kGroupChatPrefix = "group_";
constexpr const char* kP2pChatPrefix = "p2p_";
}  // namespace

const std::string& WebChannel::GetName() const {
    static const std::string name = kWebChannelName;
    return name;
}

bool WebChannel::Start() {
    role_ = ProsophorConfig::GetInstance().web.role;
    if (role_.empty()) role_ = "default";
    running_.store(true);
    LOG_INFO("WebChannel started (role='{}')", role_);
    return true;
}

void WebChannel::Stop() {
    running_.store(false);
    std::lock_guard<std::mutex> lock(registry_mutex_);
    peers_.clear();
    peers_by_user_.clear();
    LOG_INFO("WebChannel stopped");
}

bool WebChannel::IsGroupChat(const std::string& chat_id) {
    return chat_id.rfind(kGroupChatPrefix, 0) == 0;
}

std::string WebChannel::UserIdOfP2p(const std::string& chat_id) {
    // chat_id 格式:p2p_{uid} 或 p2p_{uid}_{role}(用户 × 角色独立会话)。
    // uid 形如 u_xxxxxxxxxxxxxxxx(16 hex,无下划线) → 提取到第二个 '_' 为止。
    const std::string prefix = kP2pChatPrefix;
    if (chat_id.rfind(prefix, 0) != 0) return "";
    std::string rest = chat_id.substr(prefix.size());
    size_t first = rest.find('_');  // "u_" 的下划线
    if (first == std::string::npos) return rest;
    size_t second = rest.find('_', first + 1);  // uid 之后的 '_'(role 分隔)
    if (second == std::string::npos) return rest;  // 无 role(旧格式)
    return rest.substr(0, second);
}

std::string WebChannel::RoleOfP2p(const std::string& chat_id) {
    // chat_id 格式:p2p_{uid} 或 p2p_{uid}_{role}(用户 × 角色独立会话)。
    // 角色段为第二个 '_' 之后的全部;旧格式无角色段返回空。
    const std::string prefix = kP2pChatPrefix;
    if (chat_id.rfind(prefix, 0) != 0) return "";
    std::string rest = chat_id.substr(prefix.size());
    size_t first = rest.find('_');  // "u_" 的下划线
    if (first == std::string::npos) return "";
    size_t second = rest.find('_', first + 1);  // uid 之后的 '_'(role 分隔)
    if (second == std::string::npos) return "";  // 无 role(旧格式)
    return rest.substr(second + 1);
}

// ── 连接注册表 ──────────────────────────────────────────────────────────────

void WebChannel::AttachConnection(const std::string& conn_id, const std::string& user_id,
                                  const std::string& display_name,
                                  std::function<bool(const std::string&)> send) {
    auto peer = std::make_shared<WsPeer>();
    peer->conn_id = conn_id;
    peer->user_id = user_id;
    peer->display_name = display_name;
    peer->send = std::move(send);

    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        peers_[conn_id] = peer;
        peers_by_user_[user_id].push_back(peer);
    }
    LOG_INFO("WebChannel: connection '{}' attached (user={})", conn_id, user_id);
}

void WebChannel::DetachConnection(const std::string& conn_id) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = peers_.find(conn_id);
    if (it == peers_.end()) return;

    const std::string user_id = it->second->user_id;
    peers_.erase(it);

    auto& user_peers = peers_by_user_[user_id];
    user_peers.erase(std::remove_if(user_peers.begin(), user_peers.end(),
                                    [&conn_id](const auto& p) { return p->conn_id == conn_id; }),
                     user_peers.end());
    if (user_peers.empty()) {
        peers_by_user_.erase(user_id);
    }
    LOG_INFO("WebChannel: connection '{}' detached (user={})", conn_id, user_id);
}

void WebChannel::SetGroupMemberProvider(
    std::function<std::vector<std::string>(const std::string& gid)> provider) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    group_member_provider_ = std::move(provider);
}

void WebChannel::SetGroupRoleProvider(
    std::function<std::optional<std::string>(const std::string& gid)> provider) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    group_role_provider_ = std::move(provider);
}

// ── 推送路由 ────────────────────────────────────────────────────────────────

std::vector<std::shared_ptr<WsPeer>> WebChannel::PeersForChat(const ImChatContext& ctx) const {
    std::vector<std::shared_ptr<WsPeer>> result;
    std::lock_guard<std::mutex> lock(registry_mutex_);

    if (IsGroupChat(ctx.chat_id)) {
        std::string gid = ctx.chat_id.substr(std::string(kGroupChatPrefix).size());
        std::vector<std::string> members;
        if (group_member_provider_) {
            members = group_member_provider_(gid);
        }
        for (const auto& uid : members) {
            auto it = peers_by_user_.find(uid);
            if (it != peers_by_user_.end()) {
                result.insert(result.end(), it->second.begin(), it->second.end());
            }
        }
    } else {
        // 私聊:ctx.owner_id == chat_id == p2p_{user_id},推给该用户全部连接(多标签页全推)
        std::string user_id = UserIdOfP2p(ctx.chat_id);
        if (!user_id.empty()) {
            auto it = peers_by_user_.find(user_id);
            if (it != peers_by_user_.end()) {
                result.insert(result.end(), it->second.begin(), it->second.end());
            }
        }
    }
    return result;
}

void WebChannel::PushToPeer(const std::shared_ptr<WsPeer>& peer, const std::string& json_text) {
    if (!peer->send || !peer->send(json_text)) {
        // 发送失败/连接已断:交还 ws_handler 清理(推送侧不持有销毁权)
        LOG_DEBUG("WebChannel: send failed on connection '{}', will be detached by handler",
                  peer->conn_id);
    }
}

void WebChannel::PushToAll(const std::vector<std::shared_ptr<WsPeer>>& peers,
                           const std::string& json_text) {
    for (const auto& peer : peers) {
        PushToPeer(peer, json_text);
    }
}

void WebChannel::OnAgentOutput(const ImChatContext& ctx, AgentRuntimeState state,
                               const std::string& state_msg,
                               const std::string& delta,
                               const std::optional<MessageSchema>& reply) {
    if (!running_.load()) return;

    const std::string& chat_id = ctx.chat_id;
    const std::string& session_id = ctx.session_id;
    const std::string state_name = StateToString(state);

    // 逐状态组装帧(顺序即发送顺序;前端按 session 分桶消费)
    std::vector<std::string> frames;
    switch (state) {
        case AgentRuntimeState::STREAM_THINKING_START:
        case AgentRuntimeState::STREAM_CONTENT_START:
            // 流开始:开桶 + 状态
            frames.push_back(MakeStreamStartFrame(chat_id, session_id, ""));
            frames.push_back(MakeStateFrame(chat_id, session_id, state_name, state_msg));
            break;
        case AgentRuntimeState::STREAM_CONTENT_TYPING:
            frames.push_back(MakeStateFrame(chat_id, session_id, state_name, state_msg));
            if (!delta.empty()) frames.push_back(MakeDeltaFrame(chat_id, session_id, delta));
            break;
        case AgentRuntimeState::STREAM_THINKING:
            // 思考增量不推给用户(正文桶只收正文 delta);仅透出思考状态
            frames.push_back(MakeStateFrame(chat_id, session_id, state_name, state_msg));
            break;
        case AgentRuntimeState::STREAM_TOOL_START:
        case AgentRuntimeState::STREAM_TOOL:
        case AgentRuntimeState::STREAM_TOOL_END:
            // 工具内容缓冲中,不返回给用户;只透出状态
            frames.push_back(MakeStateFrame(chat_id, session_id, state_name, state_msg));
            break;
        case AgentRuntimeState::EXECUTING_TOOL:
        case AgentRuntimeState::TOOL_USE:
            frames.push_back(MakeStateFrame(chat_id, session_id, state_name, state_msg));
            if (!delta.empty()) frames.push_back(MakeDeltaFrame(chat_id, session_id, delta));
            break;
        case AgentRuntimeState::WAITING_PERMISSION:
            frames.push_back(MakeStateFrame(chat_id, session_id, state_name, state_msg));
            break;
        case AgentRuntimeState::STATE_ERROR:
            frames.push_back(MakeErrorFrame("session_error", state_msg, chat_id));
            frames.push_back(MakeStateFrame(chat_id, session_id, state_name, state_msg));
            break;
        case AgentRuntimeState::STREAM_CONTENT_END:
            // 内容流结束;消息桶由 reply 终结,这里只清状态
            frames.push_back(MakeStateFrame(chat_id, session_id, "idle", ""));
            break;
        case AgentRuntimeState::STREAM_MODE_COMPLETE:
        case AgentRuntimeState::COMPLETE:
            if (reply.has_value()) {
                frames.push_back(MakeReplyFrame(chat_id, session_id, reply->text(),
                                                CurrentTimestampMs()));
            }
            frames.push_back(MakeStateFrame(chat_id, session_id, "idle", ""));
            break;
        default:
            break;  // IDLE / BEGINNING:不推送
    }

    if (frames.empty()) return;
    auto peers = PeersForChat(ctx);
    if (peers.empty()) return;
    for (const auto& frame : frames) {
        PushToAll(peers, frame);
    }
}

void WebChannel::SendError(const ImChatContext& ctx, const std::string& error) {
    auto peers = PeersForChat(ctx);
    if (peers.empty()) return;
    PushToAll(peers, MakeErrorFrame("chat_error", error, ctx.chat_id));
}

// ── 入站 ────────────────────────────────────────────────────────────────────

void WebChannel::InboundSend(const std::string& conn_id, const ClientSend& send) {
    std::string user_id;
    std::string display_name;
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        auto it = peers_.find(conn_id);
        if (it == peers_.end()) return;
        user_id = it->second->user_id;
        display_name = it->second->display_name;
    }

    ImMessage msg;
    msg.channel = kWebChannelName;
    msg.bot_name = "";
    msg.chat_id = send.chat_id;
    msg.chat_type = IsGroupChat(send.chat_id) ? "group" : "p2p";

    // 角色解析:私聊取 chat_id 的 role 段(用户 × 角色独立会话);群聊取群绑定角色;
    // 旧格式 chat_id / 未绑定群回退 config.web.role。角色有效性由 CreateSession 校验。
    if (msg.chat_type == "group") {
        std::string gid = send.chat_id.substr(std::string(kGroupChatPrefix).size());
        std::optional<std::string> group_role;
        {
            std::lock_guard<std::mutex> lock(registry_mutex_);
            if (group_role_provider_) group_role = group_role_provider_(gid);
        }
        msg.role = (group_role && !group_role->empty()) ? *group_role : role_;
    } else {
        std::string p2p_role = RoleOfP2p(send.chat_id);
        msg.role = p2p_role.empty() ? role_ : p2p_role;
    }

    msg.sender_id = user_id;
    msg.sender_name = display_name;
    msg.text = send.text;
    msg.message_id = send.client_msg_id;
    DispatchInbound(std::move(msg));
}

// ── 在线查询 / presence ─────────────────────────────────────────────────────

bool WebChannel::IsOnline(const std::string& user_id) const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = peers_by_user_.find(user_id);
    return it != peers_by_user_.end() && !it->second.empty();
}

std::vector<std::shared_ptr<WsPeer>> WebChannel::PeersOf(const std::string& user_id) const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = peers_by_user_.find(user_id);
    if (it == peers_by_user_.end()) return {};
    return it->second;
}

std::vector<std::string> WebChannel::OnlineUserIdsForChat(const std::string& chat_id) const {
    std::vector<std::string> result;
    std::lock_guard<std::mutex> lock(registry_mutex_);
    if (IsGroupChat(chat_id)) {
        std::string gid = chat_id.substr(std::string(kGroupChatPrefix).size());
        std::vector<std::string> members;
        if (group_member_provider_) {
            members = group_member_provider_(gid);
        }
        for (const auto& uid : members) {
            auto it = peers_by_user_.find(uid);
            if (it != peers_by_user_.end() && !it->second.empty()) {
                result.push_back(uid);
            }
        }
    } else {
        std::string user_id = UserIdOfP2p(chat_id);
        if (!user_id.empty() && IsOnline(user_id)) {
            result.push_back(user_id);
        }
    }
    return result;
}

void WebChannel::BroadcastPresence(const std::string& chat_id, const std::string& user_id,
                                   bool online) {
    if (!running_.load()) return;
    if (!IsGroupChat(chat_id)) return;

    std::vector<std::shared_ptr<WsPeer>> members;
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        std::string gid = chat_id.substr(std::string(kGroupChatPrefix).size());
        std::vector<std::string> member_ids;
        if (group_member_provider_) {
            member_ids = group_member_provider_(gid);
        }
        for (const auto& uid : member_ids) {
            if (uid == user_id) continue;  // 自身不通知
            auto it = peers_by_user_.find(uid);
            if (it != peers_by_user_.end()) {
                members.insert(members.end(), it->second.begin(), it->second.end());
            }
        }
    }
    if (members.empty()) return;
    PushToAll(members, MakePresenceFrame(chat_id, user_id, online));
}

}  // namespace prosophor
