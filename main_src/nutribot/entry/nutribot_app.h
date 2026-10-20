// 产品单例：nutribot（对齐 prosophor 的 VirtualSprite/AiCoding 形态模式）
//
// 由统一入口 main_src/main.cc 按编译宏调用：
//   默认 → RunResident：常驻语音回路（主循环是唯一 aidgen 调用者；
//          ASR/TTS 走 voice_pipeline 的 worker 线程）
#pragma once

namespace prosophor::nr::nutribot {

class NutriBotApp {
public:
    // 单例（对齐 prosophor: VirtualSprite::GetInstance().Run()）
    static NutriBotApp& GetInstance() {
        static NutriBotApp inst;
        return inst;
    }

    // 运行入口；返回进程退出码
    int Run(int argc = 0, char** argv = nullptr);

private:
    NutriBotApp() = default;
    ~NutriBotApp() = default;
    NutriBotApp(const NutriBotApp&) = delete;
    NutriBotApp& operator=(const NutriBotApp&) = delete;
};

}  // namespace prosophor::nr::nutribot
