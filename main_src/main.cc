// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include <cstdio>
#include <cstdlib>

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

    int exit_code = 0;
    try {
#ifdef PROSOPHOR_NUTRIBOT
        exit_code = prosophor::nr::nutribot::NutriBotApp::GetInstance().Run();
#elif defined(PROSOPHOR_SDL_UI)
        exit_code = prosophor::VirtualSprite::GetInstance().Run();
#elif defined(PROSOPHOR_AICODING)
        exit_code = prosophor::AiCoding::GetInstance().Run();
#else
        return 1;   // 形态宏缺失（#include 处已 #error 提示）
#endif
    } catch (const std::exception& e) {
        LOG_ERROR("Fatal error: {}", e.what());
        exit_code = 1;
    }

#ifdef PROSOPHOR_NUTRIBOT
    // 跳过静态析构直接退出：QNN/fastrpc 的 teardown 与全局单例（线程池等）的
    // 析构时序竞争，实测退出时段错误。aidgen Context 本就交由 OS 回收，
    // 此处一并省去其余全局对象的收尾。日志逐行 flush，不丢输出。
    LOG_INFO("nutribot 运行结束 (exit {})，跳过静态析构由 OS 回收", exit_code);
    std::fflush(nullptr);
    std::_Exit(exit_code);
#else
    return exit_code;
#endif
}
