// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/log_wrapper.h"
#include "config/config.h"
#include "platform/platform.h"

// 产品形态选择（同一入口，编译宏决定形态；宏由 CMake 对应开关定义）:
//   PROSOPHOR_NUTRIBOT  -> NutriBot（QNN 本地 NPU 营养管家）
//   PROSOPHOR_SDL_UI    -> 桌面精灵
//   PROSOPHOR_AICODING  -> AI 编程助手（终端）
#ifdef PROSOPHOR_NUTRIBOT
#include "nutribot/nutribot_app.h"
#elif defined(PROSOPHOR_SDL_UI)
#include "virtual_sprite/virtual_sprite.h"
#elif defined(PROSOPHOR_AICODING)
#include "ai_coding.h"
#else
#error "未启用任何应用形态宏：PROSOPHOR_NUTRIBOT / PROSOPHOR_SDL_UI / PROSOPHOR_AICODING"
#endif

// 统一入口：所有平台都用 main()
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    prosophor::Platform::SetConsoleUtf8();

    if (prosophor::Platform::IsAlreadyRunning()) {
        fprintf(stderr, "Prosophor is already running.\n");
        return 1;
    }

    const auto& config = prosophor::ProsophorConfig::GetInstance();
    prosophor::InitLog(config.log_level);
    LOG_DEBUG("Prosophor v{}", PROSOPHOR_VERSION);

    try {
#ifdef PROSOPHOR_NUTRIBOT
        return prosophor::nr::nutribot::NutriBotApp::GetInstance().Run();
#elif defined(PROSOPHOR_SDL_UI)
        return prosophor::VirtualSprite::GetInstance().Run();
#elif defined(PROSOPHOR_AICODING)
        return prosophor::AiCoding::GetInstance().Run();
#else
        return 1;   // 形态宏缺失（#include 处已 #error 提示）
#endif
    } catch (const std::exception& e) {
        LOG_ERROR("Fatal error: {}", e.what());
        return 1;
    }
}
