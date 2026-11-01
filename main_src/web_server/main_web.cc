// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <chrono>
#include <thread>

#include "prosophor_core/public/agent_engine.h"
#include "common/log_wrapper.h"
#include "prosophor_core/public/config.h"
#include "web_server/web_gateway.h"
#include "platform/platform.h"
#include "web_server/web_server_app.h"

namespace {

std::atomic<bool> g_stop{false};

}  // namespace

// prosophor_web 入口:Web 多用户服务端(与桌面 exe 独立,不链接 SDL/UI 层)。
// 启动序列镜像 virtual_sprite 时序:AgentEngine → WebGateway(创建 WebChannel)
// → WebServerApp 监听;SIGINT/SIGTERM → 优雅停机。
int main(int argc, char* argv[]) {
    using namespace prosophor;

    Platform::SetConsoleUtf8();

    // ── 命令行覆盖(--host/--port,优先于配置文件;局域网访问用 --host 0.0.0.0)──
    std::string host_override, port_override;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) host_override = argv[++i];
        else if (arg == "--port" && i + 1 < argc) port_override = argv[++i];
    }

    const auto& config = ProsophorConfig::GetInstance();
    InitLog(config.log_level);
    LOG_DEBUG("Prosophor Web v{}", PROSOPHOR_VERSION);

    // 单实例检查:与桌面共用锁会互斥(无法同机共存),web 跳过 IsAlreadyRunning,
    // 数据目录并发由会话 JSONL per-path 锁兜底(约定:同一数据目录一个进程)。
    // Ctrl+C/关窗 → 优雅停机(std::signal 在多线程 + httplib 下不可靠,走 Platform)
    Platform::SetConsoleSignalHandler([]() { g_stop.store(true); });

    // 显式运行 prosophor_web 即启动意图,不受 web.enabled 门禁
    // (web.enabled 仅为配置兼容保留;桌面端已不接线 IM 渠道)。
    try {
        // ── 核心初始化(与桌面端一致)──
        AgentEngine::GetInstance();

        // ── IM 网关:创建 WebChannel + AddOutputCallback(多前端共存)──
        WebGateway::GetInstance().Start();

        // ── Web 服务(host/port 命令行覆盖)──
        WebConfig web_config = config.web;
        if (!host_override.empty()) web_config.host = host_override;
        if (!port_override.empty()) web_config.port = std::stoi(port_override);
        WebServerApp app(web_config);
        if (!app.Start()) {
            LOG_ERROR("Web server failed to start");
            return 1;
        }

        // ── 主循环:等待信号 ──
        while (!g_stop.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        app.Stop();
        WebGateway::GetInstance().Stop();
    } catch (const std::exception& e) {
        LOG_ERROR("Fatal error: {}", e.what());
        return 1;
    }

    LOG_INFO("Prosophor Web exited cleanly");
    return 0;
}
