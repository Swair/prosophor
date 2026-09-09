// 产品单例：nutribot（对齐 prosophor 的 VirtualSprite/AiCoding 形态模式）
//
// 由统一入口 main_src/main.cc 按编译宏 PROSOPHOR_NUTRITION_ROBOT 调用；
// 本模块独立构建时由 main.cc(模块内) 调用。Run() 目前为阶段 1 demo，
// 阶段 2+ 在 Run() 内装配真实感知/语音端口（infrastructure 层注入）。
#pragma once

namespace prosophor::nr::nutribot {

class NutriBotApp {
public:
    // 单例（对齐 prosophor: VirtualSprite::GetInstance().Run()）
    static NutriBotApp& GetInstance() {
        static NutriBotApp inst;
        return inst;
    }

    // 运行主循环；返回进程退出码
    int Run();

private:
    NutriBotApp() = default;
    ~NutriBotApp() = default;
    NutriBotApp(const NutriBotApp&) = delete;
    NutriBotApp& operator=(const NutriBotApp&) = delete;
};

}  // namespace prosophor::nr::nutribot
