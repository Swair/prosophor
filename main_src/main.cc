// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/log_wrapper.h"
#include "common/config.h"
#include "platform/platform.h"

#ifdef PROSOPHOR_SDL_UI
#include "scene/sdl_app.h"
#else
#include "cli/agent_commander.h"
#endif

// 统一入口：所有平台都用 main()
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;  // 未使用参数

    prosophor::platform::SetConsoleUtf8();

    const auto& config = prosophor::ProsophorConfig::GetInstance();
    prosophor::InitLog(config.log_level);
    LOG_DEBUG("Prosophor v{}", PROSOPHOR_VERSION);

    try {
#ifdef PROSOPHOR_SDL_UI
        return prosophor::SdlApp::GetInstance().Run();
#else
        return prosophor::AgentCommander::GetInstance().Run();
#endif
    } catch (const std::exception& e) {
        LOG_ERROR("Fatal error: {}", e.what());
        return 1;
    }
}
