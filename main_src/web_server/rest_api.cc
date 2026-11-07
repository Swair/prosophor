// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "web_server/rest_api.h"

#include <algorithm>

#include "prosophor_core/public/agent_engine.h"
#include "common/log_wrapper.h"
#include "web_server/web_gateway.h"
#include "web_server/web_channel.h"
#include "web_server/web_protocol.h"
#include "web_server/session_history.h"

namespace prosophor {

void JsonResponse(httplib::Response& res, const nlohmann::json& body, int status) {
    res.status = status;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

void JsonError(httplib::Response& res, int status, const std::string& code,
               const std::string& message) {
    nlohmann::json body;
    body["error"] = {{"code", code}, {"message", message}};
    JsonResponse(res, body, status);
}

std::optional<WebUser> Authenticate(const httplib::Request& req, httplib::Response& res,
                                    const UserStore& users) {
    std::string token;
    // 优先 Authorization: Bearer <token>
    auto auth = req.get_header_value("Authorization");
    const std::string prefix = "Bearer ";
    if (auth.size() > prefix.size() && auth.compare(0, prefix.size(), prefix) == 0) {
        token = auth.substr(prefix.size());
    } else {
        // 兜底:导航认证的 cookie(prosophor_token)
        auto cookie = req.get_header_value("Cookie");
        const std::string key = "prosophor_token=";
        size_t pos = cookie.find(key);
        if (pos != std::string::npos) {
            token = cookie.substr(pos + key.size());
            size_t end = token.find(';');
            if (end != std::string::npos) token = token.substr(0, end);
        }
    }
    if (token.empty()) {
        JsonError(res, 401, "unauthorized", "missing or malformed Authorization header");
        return std::nullopt;
    }
    auto user = users.ValidateToken(token);
    if (!user) {
        JsonError(res, 401, "unauthorized", "invalid or expired token");
        return std::nullopt;
    }
    return user;
}

void RegisterRestApi(httplib::Server& svr, UserStore& users, GroupStore& groups,
                     const WebConfig& /*config*/, const std::string& sessions_dir) {
    // shared_ptr 捕获进 lambda(RegisterRestApi 返回后局部对象会销毁,引用悬垂)
    auto history = std::make_shared<SessionHistory>(sessions_dir);
    auto& gateway = WebGateway::GetInstance();

    // ── 会话(需认证)──
    // 可见性:私聊 ctx.chat_id == "p2p_{me}";群聊我∈成员
    // 数据源:内存会话(WebGateway) + JSONL 磁盘扫描兜底(服务器重启后恢复历史列表)
    svr.Get("/api/sessions", [&users, &groups, &gateway, history](const httplib::Request& req,
                                                                  httplib::Response& res) {
        auto user = Authenticate(req, res, users);
        if (!user) return;
        auto visible = [&groups](const std::string& chat_id, const std::string& user_id) {
            if (WebChannel::IsGroupChat(chat_id)) {
                std::string gid = chat_id.substr(std::string("group_").size());
                return groups.IsMember(gid, user_id);
            }
            return WebChannel::UserIdOfP2p(chat_id) == user_id;
        };

        std::vector<std::string> seen;
        auto arr = nlohmann::json::array();
        auto push = [&arr, &seen](const std::string& session_id, const std::string& chat_id) {
            if (std::find(seen.begin(), seen.end(), chat_id) != seen.end()) return;
            seen.push_back(chat_id);
            arr.push_back({{"session_id", session_id},
                           {"chat_id", chat_id},
                           {"chat_type", WebChannel::IsGroupChat(chat_id) ? "group" : "p2p"},
                           {"role_id", ""}});
        };

        // ① 内存会话
        for (const auto& ctx : gateway.ListChats()) {
            if (ctx.channel != "web" || !visible(ctx.chat_id, user->user_id)) continue;
            push(ctx.session_id, ctx.chat_id);
        }
        // ② 磁盘扫描兜底(owner = chat_id;重启后内存为空时恢复历史)
        for (const auto& [owner, last_ts] : history->ListOwners()) {
            (void)last_ts;
            if (!visible(owner, user->user_id)) continue;
            push(owner, owner);
        }
        JsonResponse(res, {{"sessions", arr}});
    });

    // 历史消息(游标分页:before_ts + limit)
    svr.Get(R"(/api/sessions/([^/]+)/messages)", [&users, &groups, &gateway, history](
                                                     const httplib::Request& req,
                                                     httplib::Response& res) {
        auto user = Authenticate(req, res, users);
        if (!user) return;
        std::string sid = req.matches[1].str();
        auto ctx = gateway.GetChatContext(sid);

        // 访问控制 + 归属:内存会话优先;重启后内存为空,按 sid(owner=chat_id)直接读盘
        std::string owner_id;
        if (ctx && ctx->channel == "web") {
            if (WebChannel::IsGroupChat(ctx->chat_id)) {
                std::string gid = ctx->chat_id.substr(std::string("group_").size());
                if (!groups.IsMember(gid, user->user_id)) {
                    JsonError(res, 403, "forbidden", "no access to this session");
                    return;
                }
            } else if (WebChannel::UserIdOfP2p(ctx->chat_id) != user->user_id) {
                JsonError(res, 403, "forbidden", "no access to this session");
                return;
            }
            owner_id = ctx->chat_id;
        } else {
            // 磁盘兜底:owner 即 chat_id(session_id 兼容 chat_id 直查)
            if (WebChannel::IsGroupChat(sid)) {
                std::string gid = sid.substr(std::string("group_").size());
                if (!groups.IsMember(gid, user->user_id)) {
                    JsonError(res, 403, "forbidden", "no access to this session");
                    return;
                }
            } else if (WebChannel::UserIdOfP2p(sid) != user->user_id) {
                JsonError(res, 403, "forbidden", "no access to this session");
                return;
            }
            owner_id = sid;
        }

        size_t limit = 50;
        if (req.has_param("limit")) {
            limit = std::max<size_t>(1, std::min<size_t>(200, req.get_param_value("limit").empty()
                                                              ? 50
                                                              : std::stoull(req.get_param_value("limit"))));
        }
        int64_t before_ts = 0;
        if (req.has_param("before_ts") && !req.get_param_value("before_ts").empty()) {
            before_ts = std::stoll(req.get_param_value("before_ts"));
        }

        auto msgs = history->ReadMessages(owner_id, before_ts, limit);
        auto arr = nlohmann::json::array();
        for (const auto& m : msgs) {
            nlohmann::json row;
            try {
                row = nlohmann::json::parse(m.raw_json);
            } catch (...) {
                continue;
            }
            arr.push_back(row);
        }
        JsonResponse(res, {{"session_id", sid}, {"messages", arr}});
    });

    // 停止生成
    svr.Post(R"(/api/sessions/([^/]+)/stop)", [&users, &gateway](const httplib::Request& req,
                                                                 httplib::Response& res) {
        auto user = Authenticate(req, res, users);
        if (!user) return;
        std::string sid = req.matches[1].str();
        if (!gateway.GetChatContext(sid)) {
            JsonError(res, 404, "not_found", "session not found");
            return;
        }
        AgentEngine::GetInstance().StopSession(sid);
        res.status = 204;
    });

    // ── 健康检查(免认证)──
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        JsonResponse(res, {{"status", "ok"}, {"ts", CurrentTimestampMs()}});
    });

    // ── 认证(免认证)──
    svr.Post("/api/auth/register", [&users](const httplib::Request& req,
                                            httplib::Response& res) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (...) {
            JsonError(res, 400, "bad_request", "invalid JSON body");
            return;
        }
        auto result = users.RegisterUser(body.value("username", ""),
                                         body.value("password", ""),
                                         body.value("display_name", ""));
        if (!result.ok) {
            const int status = (result.error == "username_taken") ? 409 : 400;
            JsonError(res, status, result.error, "registration failed");
            return;
        }
        JsonResponse(res, {{"user_id", result.value},
                           {"username", body.value("username", "")}},
                     201);
    });

    svr.Post("/api/auth/login", [&users](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (...) {
            JsonError(res, 400, "bad_request", "invalid JSON body");
            return;
        }
        // token TTL 用全局 web 配置(与 WS 共用)
        const int ttl_hours = ProsophorConfig::GetInstance().web.token_ttl_hours;
        auto result = users.LoginUser(body.value("username", ""), body.value("password", ""),
                                      ttl_hours);
        if (!result.ok) {
            JsonError(res, 401, result.error, "login failed");
            return;
        }
        auto user = users.GetUser(result.user_id);
        JsonResponse(res, {{"token", result.value},
                           {"user_id", user->user_id},
                           {"username", user->username},
                           {"display_name", user->display_name}});
    });

    // 设备免注册:device_id(客户端 UUID)→ 自动建号/复用 + 签发 token
    // 设备免注册:device_id(客户端 UUID)→ 自动建号/复用 + 签发 token。
    // GET 与 POST 均支持:GET(device_id 走 query)供前端 fetch 使用——
    // 实测部分浏览器/网络对局域网 IP 的 fetch POST 不发,GET 全通。
    // 注意:不能捕获 RegisterRestApi 局部对象(返回后悬垂,见文件头注释)。
    svr.Get("/api/auth/device", [&users](const httplib::Request& req, httplib::Response& res) {
        const int ttl = ProsophorConfig::GetInstance().web.token_ttl_hours;
        auto result = users.GetOrCreateByDeviceId(req.get_param_value("device_id"), ttl);
        if (!result.ok) {
            JsonError(res, 400, result.error, "device auth failed");
            return;
        }
        auto user = users.GetUser(result.user_id);
        if (!user) {
            JsonError(res, 500, "internal", "user lookup failed after auth");
            return;
        }
        JsonResponse(res, {{"token", result.value},
                           {"user_id", user->user_id},
                           {"username", user->username},
                           {"display_name", user->display_name}});
    });
    svr.Post("/api/auth/device", [&users](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (...) {
            JsonError(res, 400, "bad_request", "invalid JSON body");
            return;
        }
        const int ttl = ProsophorConfig::GetInstance().web.token_ttl_hours;
        auto result = users.GetOrCreateByDeviceId(body.value("device_id", ""), ttl);
        if (!result.ok) {
            JsonError(res, 400, result.error, "device auth failed");
            return;
        }
        auto user = users.GetUser(result.user_id);
        if (!user) {
            JsonError(res, 500, "internal", "user lookup failed after auth");
            return;
        }
        JsonResponse(res, {{"token", result.value},
                           {"user_id", user->user_id},
                           {"username", user->username},
                           {"display_name", user->display_name}});
    });

    svr.Post("/api/auth/logout", [&users](const httplib::Request& req, httplib::Response& res) {
        auto auth = req.get_header_value("Authorization");
        const std::string prefix = "Bearer ";
        if (auth.size() <= prefix.size() || auth.compare(0, prefix.size(), prefix) != 0) {
            JsonError(res, 401, "unauthorized", "missing token");
            return;
        }
        users.LogoutUser(auth.substr(prefix.size()));
        res.status = 204;
    });

    // ── 用户(需认证)──
    svr.Get("/api/me", [&users](const httplib::Request& req, httplib::Response& res) {
        auto user = Authenticate(req, res, users);
        if (!user) return;
        JsonResponse(res, {{"user_id", user->user_id},
                           {"username", user->username},
                           {"display_name", user->display_name},
                           {"created_at", user->created_at}});
    });

    // ── 角色列表(群绑定角色选择器)──
    svr.Get("/api/roles", [&users](const httplib::Request& req, httplib::Response& res) {
        if (!Authenticate(req, res, users)) return;
        JsonResponse(res, {{"roles", AgentEngine::GetInstance().ListRoles()}});
    });

    // ── 群组(需认证)──
    svr.Get("/api/groups", [&users, &groups](const httplib::Request& req,
                                             httplib::Response& res) {
        auto user = Authenticate(req, res, users);
        if (!user) return;
        auto my_groups = groups.ListGroupsForUser(user->user_id);
        auto arr = nlohmann::json::array();
        for (const auto& g : my_groups) {
            arr.push_back({{"group_id", g.group_id},
                           {"name", g.name},
                           {"description", g.description},
                           {"role_id", g.role_id},
                           {"owner_id", g.owner_id},
                           {"member_count", g.member_ids.size()}});
        }
        JsonResponse(res, {{"groups", arr}});
    });

    svr.Post("/api/groups", [&users, &groups](const httplib::Request& req,
                                              httplib::Response& res) {
        auto user = Authenticate(req, res, users);
        if (!user) return;
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (...) {
            JsonError(res, 400, "bad_request", "invalid JSON body");
            return;
        }
        auto gid = groups.CreateGroup(body.value("name", ""), body.value("description", ""),
                                      body.value("role_id", "default"), user->user_id);
        if (!gid) {
            JsonError(res, 400, "bad_request", "invalid group name");
            return;
        }
        JsonResponse(res, {{"group_id", *gid}}, 201);
    });

    svr.Get(R"(/api/groups/([^/]+))", [&users, &groups](const httplib::Request& req,
                                                        httplib::Response& res) {
        auto user = Authenticate(req, res, users);
        if (!user) return;
        std::string gid = req.matches[1].str();
        auto group = groups.GetGroup(gid);
        if (!group) {
            JsonError(res, 404, "not_found", "group not found");
            return;
        }
        if (!groups.IsMember(gid, user->user_id)) {
            JsonError(res, 403, "forbidden", "not a member of this group");
            return;
        }
        // 成员明细(display_name 从 UserStore 补全)
        auto members_arr = nlohmann::json::array();
        for (const auto& uid : group->member_ids) {
            auto member = users.GetUser(uid);
            members_arr.push_back({{"user_id", uid},
                                   {"display_name", member ? member->display_name : uid}});
        }
        JsonResponse(res, {{"group_id", group->group_id},
                           {"name", group->name},
                           {"description", group->description},
                           {"role_id", group->role_id},
                           {"owner_id", group->owner_id},
                           {"created_at", group->created_at},
                           {"members", members_arr}});
    });

    svr.Post(R"(/api/groups/([^/]+)/join)", [&users, &groups](const httplib::Request& req,
                                                              httplib::Response& res) {
        auto user = Authenticate(req, res, users);
        if (!user) return;
        std::string gid = req.matches[1].str();
        if (!groups.JoinGroup(gid, user->user_id)) {
            JsonError(res, 404, "not_found", "group not found");
            return;
        }
        res.status = 200;
        JsonResponse(res, {{"group_id", gid}});
    });

    svr.Post(R"(/api/groups/([^/]+)/leave)", [&users, &groups](const httplib::Request& req,
                                                               httplib::Response& res) {
        auto user = Authenticate(req, res, users);
        if (!user) return;
        std::string gid = req.matches[1].str();
        if (groups.IsOwner(gid, user->user_id)) {
            JsonError(res, 400, "bad_request", "owner cannot leave; delete the group instead");
            return;
        }
        if (!groups.LeaveGroup(gid, user->user_id)) {
            JsonError(res, 404, "not_found", "group not found or not a member");
            return;
        }
        res.status = 200;
        JsonResponse(res, {{"group_id", gid}});
    });

    svr.Post(R"(/api/groups/([^/]+)/members)", [&users, &groups](const httplib::Request& req,
                                                                 httplib::Response& res) {
        auto user = Authenticate(req, res, users);
        if (!user) return;
        std::string gid = req.matches[1].str();
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (...) {
            JsonError(res, 400, "bad_request", "invalid JSON body");
            return;
        }
        std::vector<std::string> user_ids;
        if (body.contains("user_ids") && body["user_ids"].is_array()) {
            for (const auto& item : body["user_ids"]) {
                user_ids.push_back(item.get<std::string>());
            }
        }
        if (!groups.AddMembers(gid, user->user_id, user_ids)) {
            JsonError(res, 403, "forbidden", "only group owner can add members");
            return;
        }
        res.status = 200;
        JsonResponse(res, {{"group_id", gid}});
    });

    svr.Delete(R"(/api/groups/([^/]+))", [&users, &groups](const httplib::Request& req,
                                                           httplib::Response& res) {
        auto user = Authenticate(req, res, users);
        if (!user) return;
        std::string gid = req.matches[1].str();
        if (!groups.DeleteGroup(gid, user->user_id)) {
            JsonError(res, 403, "forbidden", "only group owner can delete the group");
            return;
        }
        res.status = 204;
    });

    LOG_INFO("RestApi: routes registered (health, auth, me, roles, groups)");
}

}  // namespace prosophor
