// NutriBotApp::Run —— 主程序自检 demo：
//   [1] 场景状态机（确定性核心）  [2] 禁忌规则引擎
//   [3] 本地 LLM（aidgen provider，NPU）—— agent 调用链验证
// 阶段 2+ 在此装配：感知(aidlite) → MealMonitor → 语音
#include "nutribot_app.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "common/file_utils.h"
#include "core/agent_types.h"
#include "core/messages_schema.h"

#include "agent_engine.h"
#include "member.h"
#include "rule_engine.h"
#include "scene.h"

namespace prosophor::nr::nutribot {

namespace {

// 模拟一段用餐观察流（帧级），打印状态机转移轨迹
void DemoSceneMachine() {
    prosophor::nr::domain::SceneStateMachine sm;
    const int total = 150;
    for (int i = 0; i <= total; ++i) {
        prosophor::nr::domain::SceneObservation obs;
        if (i < 30)          obs = {0.15, false};  // 上菜阶段：高运动
        else if (i < 100)    obs = {0.03, true};   // 用餐阶段：人脸在场
        else                 obs = {0.00, false};  // 收桌离席
        const auto before = sm.state();
        sm.update(obs);
        if (sm.state() != before) {
            std::printf("  t=%3d  %s\n", i, sm.label().data());
        }
    }
    std::printf("  会话内状态转移次数: %d\n", sm.transitions());
}

// 禁忌规则演示：成员档案 -> 桌上菜品 -> 提醒事件
void DemoRuleEngine() {
    prosophor::nr::domain::Member child;
    child.member_id = "m1";
    child.name = "小明";
    child.profile.age = 6;
    child.profile.health_tags = {"儿童", "乳糖不耐受"};
    child.profile.taboo_foods = {"牛奶", "花生"};
    child.profile.allergies = {"虾"};

    std::vector<prosophor::nr::domain::Member> members = {child};
    std::vector<prosophor::nr::domain::FoodItem> foods = {
        prosophor::nr::domain::FoodItem("f1", "牛奶"),
        prosophor::nr::domain::FoodItem("f2", "花生酱拌面"),
        prosophor::nr::domain::FoodItem("f3", "白灼虾"),
        prosophor::nr::domain::FoodItem("f4", "清蒸鲈鱼"),   // 安全菜：不产生事件
    };

    prosophor::nr::domain::NutritionRuleEngine engine;
    const auto events = engine.check_taboos(members, foods);
    std::printf("  检出 %zu 条禁忌提醒:\n", events.size());
    for (const auto& e : events) {
        std::printf("    [%s] %s\n", e.level.c_str(), e.message.c_str());
    }
}


// [3] AgentEngine 会话自检：CreateSession(nutribot) + SendUserMessage
// LLM 调用一律经 AgentEngine（角色/路由/会话体系），不直连 provider。
void DemoEngineAgent() {
    auto& engine = prosophor::AgentEngine::GetInstance();
    std::atomic<bool> done{false};

    engine.AddOutputCallback(
        [&done](const std::string& /*session*/, const std::string& /*role*/,
                prosophor::AgentRuntimeState state, const std::string& state_msg,
                const std::optional<prosophor::MessageSchema>& reply,
                const std::string& delta) {
            switch (state) {
                case prosophor::AgentRuntimeState::BEGINNING:
                    std::printf("\n  ● [开始] %s\n", state_msg.c_str());
                    break;
                case prosophor::AgentRuntimeState::STREAM_THINKING_START:
                    std::printf("\n  ○ [思考] ");
                    break;
                case prosophor::AgentRuntimeState::STREAM_THINKING:
                    std::fputs(delta.c_str(), stdout);
                    std::fflush(stdout);
                    break;
                case prosophor::AgentRuntimeState::STREAM_THINKING_END:
                    std::printf(" [思考结束]\n");
                    break;
                case prosophor::AgentRuntimeState::STREAM_CONTENT_START:
                    std::printf("\n  ● [回答] ");
                    break;
                case prosophor::AgentRuntimeState::STREAM_CONTENT_TYPING:
                    std::fputs(delta.c_str(), stdout);
                    std::fflush(stdout);
                    break;
                case prosophor::AgentRuntimeState::STREAM_CONTENT_END:
                    std::printf(" [回答结束]\n");
                    break;
                case prosophor::AgentRuntimeState::EXECUTING_TOOL:
                    std::printf("  ◇ [工具] %s\n", state_msg.c_str());
                    break;
                case prosophor::AgentRuntimeState::STREAM_MODE_COMPLETE:
                case prosophor::AgentRuntimeState::COMPLETE:
                    std::printf("\n");
                    if (reply && !reply->text().empty()) {
                        std::printf("  [完整回复] %s\n", reply->text().c_str());
                    } else {
                        std::printf("  [会话完成]\n");
                    }
                    done.store(true);
                    break;
                case prosophor::AgentRuntimeState::STATE_ERROR:
                    std::printf("\n  会话错误: %s\n", state_msg.c_str());
                    done.store(true);
                    break;
                default:
                    break;
            }
        });

    const std::string session_id =
        engine.CreateSession("nutribot", "NutriBot 自检会话");
    std::printf("  会话 %s 已创建（角色 nutribot，默认 LLM provider）\n",
                session_id.c_str());
    std::printf("  >>> 你：请用一句话介绍你能做什么\n  <<< ");
    engine.SendUserMessage(session_id, "请用一句话介绍你能做什么");

    // 等待会话完成（回调置位），上限 300s
    for (int i = 0; i < 3000 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!done.load()) {
        std::printf("\n  等待会话回复超时\n");
    }
}

}  // namespace

int NutriBotApp::Run() {
    std::printf("nutribot (C++ demo)\n");
    std::printf("[1] 场景状态机轨迹:\n");
    DemoSceneMachine();
    std::printf("[2] 禁忌规则引擎:\n");
    DemoRuleEngine();
    std::printf("[3] AgentEngine 会话自检（nutribot 角色 + aidgen provider）:\n");
    DemoEngineAgent();
    return 0;
}

}  // namespace prosophor::nr::nutribot
