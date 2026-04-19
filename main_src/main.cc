// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/log_wrapper.h"
#include "common/config.h"

#ifdef AICODE_SDL_UI
#include "scene/sdl_app.h"
#else
#include "cli/agent_commander.h"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#ifdef _WIN32
    // Set console code page to UTF-8 for proper Chinese character display
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    const auto& config = aicode::AiCodeConfig::GetInstance();
    aicode::InitLog(config.log_level);
    LOG_INFO("AiCode v{}", AICODE_VERSION);

    try {
#ifdef AICODE_SDL_UI
        // SDL 图形界面模式
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
