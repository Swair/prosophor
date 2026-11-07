// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>

#include <httplib.h>

#include "common/noncopyable.h"
#include "prosophor_core/public/config.h"

namespace prosophor {

class WebChannel;
class WsHandler;
class WebGateway;
class UserStore;
class GroupStore;

/// Web 服务端顶层编排:HTTP 路由 + WS 路由 + 静态文件托管 + 用户/群组存储。
/// 依赖 prosophor_core;不碰 media_engine/components 等 UI 层。
///
/// 生命周期:main_web.cc 先 AgentEngine + WebGateway::Start()(创建 WebChannel),
/// 再构造本类 Start()(注册路由 → bind → 阻塞监听线程);Stop() 由信号驱动。
class WebServerApp : public Noncopyable {
public:
    explicit WebServerApp(const WebConfig& config);
    ~WebServerApp();

    /// 注册全部路由并开始监听(内部起监听线程);bind 失败返回 false。
    bool Start();

    /// 停止监听并等待监听线程退出。
    void Stop();

    bool running() const { return running_.load(); }
    int port() const { return config_.port; }

private:
    /// 数据目录:config.data_dir 非空用它;空 → ~/.prosophor/web。
    std::string ResolveDataDir() const;
    /// 解析静态资源根目录:web_root 非空用它;空 → exe 相邻 web-dist。
    std::string ResolveWebRoot() const;
    /// 注册静态文件 + SPA fallback。
    void RegisterStaticRoutes(httplib::Server& svr, const std::string& web_root);

    WebConfig config_;
    WebGateway& gateway_;
    WebChannel* channel_ = nullptr;
    std::unique_ptr<UserStore> users_;
    std::unique_ptr<GroupStore> groups_;
    std::unique_ptr<WsHandler> ws_handler_;
    httplib::Server svr_;
    std::atomic<bool> running_{false};
    std::thread listen_thread_;
};

}  // namespace prosophor
