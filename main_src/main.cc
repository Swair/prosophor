// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/log_wrapper.h"
#include "common/config.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef AICODE_SDL_UI
#include "scene/sdl_app.h"
#else
#include "cli/agent_commander.h"
#endif

// 统一入口：所有平台都用 main()
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;  // 未使用参数

#ifdef _WIN32
    // 设置 Windows 控制台代码页为 UTF-8，解决中文乱码问题
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    const auto& config = aicode::AiCodeConfig::GetInstance();
    aicode::InitLog(config.log_level);
    LOG_INFO("AiCode v{}", AICODE_VERSION);

    try {
#ifdef AICODE_SDL_UI
        return aicode::SdlApp::GetInstance().Run();
#else
        // 命令行 Agent 模式
        return aicode::AgentCommander::GetInstance().Run();
#endif
    } catch (const std::exception& e) {
        LOG_ERROR("Fatal error: {}", e.what());
        return 1;
    }
}
