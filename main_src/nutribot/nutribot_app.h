// 产品单例：nutribot（对齐 prosophor 的 VirtualSprite/AiCoding 形态模式）
//
// 由统一入口 main_src/main.cc 按编译宏调用。两种运行模式：
//   默认        → RunResident：常驻语音回路（Phase 1 语音闭环，主循环是唯一
//                 aidgen 调用者；ASR/TTS 走 voice_session 的 worker 线程）
//   --selftest  → RunSelfTest：阶段 1 确定性 demo（原 Run() 内容，零功能改动）
#pragma once

namespace prosophor::nr::nutribot {

class NutriBotApp {
public:
    // 单例（对齐 prosophor: VirtualSprite::GetInstance().Run()）
    static NutriBotApp& GetInstance() {
        static NutriBotApp inst;
        return inst;
    }

    // 运行入口；返回进程退出码。argv 含 --selftest 时跑确定性自检
    int Run(int argc = 0, char** argv = nullptr);

private:
    // 阶段 1 自检 demo（确定性核心端到端用例编排，[1]~[6] 段）
    int RunSelfTest();

    // 常驻语音回路：麦克风 → VAD → ASR → AgentEngine → TTS → 扬声器
    int RunResident();

    NutriBotApp() = default;
    ~NutriBotApp() = default;
    NutriBotApp(const NutriBotApp&) = delete;
    NutriBotApp& operator=(const NutriBotApp&) = delete;
};

}  // namespace prosophor::nr::nutribot
