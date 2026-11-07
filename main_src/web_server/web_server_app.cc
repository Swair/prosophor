// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "web_server/web_server_app.h"

#include <chrono>
#include <optional>

#include <httplib.h>

#include "common/file_utils.h"
#include "common/log_wrapper.h"
#include "prosophor_core/public/config.h"
#include "platform/platform.h"
#include "web_server/web_gateway.h"
#include "web_server/web_channel.h"
#include "web_server/group_store.h"
#include "web_server/rest_api.h"
#include "web_server/user_store.h"
#include "web_server/ws_handler.h"

namespace prosophor {

WebServerApp::WebServerApp(const WebConfig& config)
    : config_(config), gateway_(WebGateway::GetInstance()) {
    svr_.set_websocket_ping_interval(30);
    svr_.set_websocket_max_missed_pongs(2);

    // HTTP 访问日志(定位"请求是否到达服务器",含来源 IP)
    svr_.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        LOG_INFO("[http] {} {} -> {} from {}", req.method, req.path, res.status,
                 req.remote_addr);
    });

    // handler 异常 → 500 + 错误日志(定位用)
    svr_.set_exception_handler([](const httplib::Request& req, httplib::Response& res,
                                  std::exception_ptr ep) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            LOG_ERROR("httplib handler exception for {}: {}", req.path, e.what());
        } catch (...) {
            LOG_ERROR("httplib handler exception for {}: unknown", req.path);
        }
        res.status = 500;
        res.set_content(R"({"error":{"code":"internal","message":"internal error"}})",
                        "application/json");
    });

    // 用户/群组存储(纯文件,启动即加载)
    const std::string data_dir = ResolveDataDir();
    users_ = std::make_unique<UserStore>(data_dir);
    groups_ = std::make_unique<GroupStore>(data_dir);
    users_->Load();
    groups_->Load();
}

std::string WebServerApp::ResolveDataDir() const {
    if (!config_.data_dir.empty()) {
        return ExpandHome(config_.data_dir);
    }
    return (ProsophorConfig::BaseDir() / "web").string();
}

WebServerApp::~WebServerApp() {
    Stop();
}

std::string WebServerApp::ResolveWebRoot() const {
    if (!config_.web_root.empty()) {
        return ExpandHome(config_.web_root);
    }
    // 默认:exe 相邻 web-dist(与 install 布局 bin/web-dist 一致)
    return (ProsophorConfig::InstallConfigDir().parent_path() / "web-dist").string();
}

void WebServerApp::RegisterStaticRoutes(httplib::Server& svr, const std::string& web_root) {
    if (!DirExists(web_root)) {
        LOG_WARN("WebServer: web_root '{}' not found; frontend not served", web_root);
        return;
    }

    // 静态托管 + SPA fallback 合一:
    // httplib 的 set_mount_point 会先于其他 handler 短路(文件不存在直接 404),
    // 导致 SPA fallback 永远不生效(/chat/:roleId 刷新 404)。改为单 handler:
    // 命中静态文件 → 返回文件;未命中且非 /api → index.html(前端路由接管)。
    // 注意:必须最后注册(精确的 /api/* 与 /ws 已先注册,httplib 按注册顺序匹配)。
    // 导航级免注册:URL 带 ?device_id= 时完成认证并 Set-Cookie(浏览器扩展/隐私
    // 设置会拦截页面内 fetch,但不拦页面导航;认证走导航 + cookie 绕开拦截)。
    svr.Get(R"(/(.*))", [web_root, this](const httplib::Request& req, httplib::Response& res) {
        std::string rel = req.matches[1].str();
        if (rel.empty()) rel = "index.html";
        if (rel.rfind("api/", 0) == 0) {
            return;  // 未注册的 API 交给默认 404
        }
        // 防目录穿越
        if (rel.find("..") != std::string::npos || rel.find('\\') != std::string::npos) {
            res.status = 400;
            return;
        }

        // 导航级免注册(仅首页;带 device_id → 建号/复用 + Set-Cookie)
        if (rel == "index.html") {
            std::string device_id = req.get_param_value("device_id");
            if (!device_id.empty() && users_) {
                const int ttl = ProsophorConfig::GetInstance().web.token_ttl_hours;
                auto result = users_->GetOrCreateByDeviceId(device_id, ttl);
                if (result.ok) {
                    res.set_header("Set-Cookie",
                                   "prosophor_token=" + result.value +
                                       "; Path=/; Max-Age=" + std::to_string(ttl * 3600) +
                                       "; SameSite=Lax");
                    LOG_INFO("导航免注册: device={} -> token issued", device_id);
                }
            }
        }

        std::string file = web_root + "/" + rel;
        if (FileExists(file)) {
            res.set_file_content(file);
            return;
        }
        // SPA fallback
        std::string index = web_root + "/index.html";
        if (FileExists(index)) {
            res.set_file_content(index);
        } else {
            res.status = 404;
        }
    });
}

bool WebServerApp::Start() {
    if (running_.load()) return true;

    // ── 渠道:WebChannel 由 WebGateway::Start() 创建(web.enabled 时)──
    channel_ = dynamic_cast<WebChannel*>(gateway_.GetChannel("web"));
    if (!channel_) {
        LOG_ERROR("WebServer: web channel not created (WebGateway not started / web.enabled=false)");
        return false;
    }

    // 群成员查询注入 WebChannel(分层:GroupStore 在 web_server 层,经回调注入)
    channel_->SetGroupMemberProvider([this](const std::string& gid) {
        return groups_->MemberIds(gid);
    });
    // 群绑定角色注入:群消息以群角色建会话(建群时 role_id 定死,未绑定回退 web.role)
    channel_->SetGroupRoleProvider([this](const std::string& gid)
                                       -> std::optional<std::string> {
        auto group = groups_->GetGroup(gid);
        if (!group || group->role_id.empty()) return std::nullopt;
        return group->role_id;
    });

    // ── 路由:先 REST(精确匹配),再 WS,最后静态 fallback ──
    // generic_string:统一正斜杠,避免 Windows 混合分隔符差异
    RegisterRestApi(svr_, *users_, *groups_, config_,
                    (ProsophorConfig::BaseDir() / "sessions").generic_string());
    ws_handler_ = std::make_unique<WsHandler>(*channel_, gateway_, *users_, *groups_);
    ws_handler_->Register(svr_);
    RegisterStaticRoutes(svr_, ResolveWebRoot());

    // ── 绑定端口(失败立即返回,不阻塞)──
    if (!svr_.bind_to_port(config_.host, config_.port)) {
        LOG_ERROR("WebServer: failed to bind {}:{}", config_.host, config_.port);
        return false;
    }

    // 访问地址:通配 host(0.0.0.0/::)显示 localhost,用户可直接打开
    std::string display_host = config_.host;
    if (display_host == "0.0.0.0" || display_host == "::") display_host = "localhost";
    LOG_INFO("Prosophor Web 已启动,访问地址: http://{}:{}/", display_host, config_.port);
    // 绑定所有网卡时,打印本机局域网 IP(手机同 WiFi 访问)
    if (display_host == "localhost") {
        std::string lan = Platform::GetLanAddresses();
        if (!lan.empty()) LOG_INFO("局域网访问: http://{}:{}/", lan, config_.port);
    }

    running_.store(true);
    listen_thread_ = std::thread([this]() {
        LOG_INFO("WebServer: listening on {}:{}", config_.host, config_.port);
        svr_.listen_after_bind();
        LOG_INFO("WebServer: listener stopped");
    });

    return true;
}

void WebServerApp::Stop() {
    if (!running_.exchange(false)) return;
    if (svr_.is_running()) {
        svr_.stop();
    }
    if (listen_thread_.joinable()) {
        listen_thread_.join();
    }
    LOG_INFO("WebServer: stopped");
}

}  // namespace prosophor
