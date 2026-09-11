// NutriBotApp::Run —— 主程序自检 demo（确定性核心端到端用例编排）：
//   [1] 场景状态机轨迹                —— 场景 FSM（确定性核心）
//   [2] 禁忌规则引擎                  —— 规则比对（确定性核心）
//   [3] 注册用例                      —— 对话式画像采集 -> 档案落盘读回
//   [4] 用餐监控用例                  —— MealMonitor + Fake 感知 -> 场景包/提醒 -> 餐后小结
//   [5] LLM 会话自检                  —— agent 调用链（aidgen provider，NPU）
// 感知替换点：阶段 2+ 把 [4] 的 Fake 端口换成 aidlite/qwen3vl 同名实现（读真实帧），
// MealMonitor 与用例编排零改动；语音（ASR/TTS）就绪后替换终端问答。
#include "nutribot_app.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "common/file_utils.h"
#include "core/agent_types.h"
#include "core/messages_schema.h"

#include "agent_engine.h"
#include "events.h"
#include "food.h"
#include "meal_monitor.h"
#include "member.h"
#include "member_repository.h"
#include "ports.h"
#include "quiz_cli.h"
#include "rule_engine.h"
#include "scene.h"
#include "vl_food_detector.h"

#include "providers/llm/aidgen_provider.h"
#include "providers/llm/llm_provider.h"
#include "providers/provider_router/llm_provider_router.h"

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
            LOG_INFO("  t={:3}  {}", i, sm.label().data());
        }
    }
    LOG_INFO("  会话内状态转移次数: {}", sm.transitions());
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
    LOG_INFO("  检出 {} 条禁忌提醒:", events.size());
    for (const auto& e : events) {
        LOG_INFO("    [{}] {}", e.level.c_str(), e.message.c_str());
    }
}


// ================= [3] 注册用例 / [4] 用餐监控用例的支撑件 =================

// 脚本化对话端口：按预设答案逐题回应，打印问答轨迹。
// Phase 6 换 ASR/TTS 后由真实 DialoguePort 实现替代，ProfileQuiz 编排不变。
class ScriptedDialoguePort : public prosophor::nr::app::DialoguePort {
public:
    explicit ScriptedDialoguePort(std::vector<std::string> answers)
        : answers_(answers.begin(), answers.end()) {}

    std::optional<std::string> ask(const std::string& question) override {
        LOG_INFO("    [问] {}", question.c_str());
        if (answers_.empty()) {
            LOG_INFO("    [答] (无回应)");
            return std::nullopt;
        }
        std::string answer = std::move(answers_.front());
        answers_.pop_front();
        LOG_INFO("    [答] {}", answer.c_str());
        return answer;
    }

private:
    std::deque<std::string> answers_;
};

// 注册案例：姓名 + 8 问脚本化回答（与单测 test_register_flow 同构，多一人）。
struct RegistrationCase {
    const char* name;
    std::vector<std::string> answers;
};

// 脚本帧：第 t 帧的感知真相（帧差运动、在场人脸名单、桌上菜品）。
// 真实感知（aidlite/qwen3vl，阶段 2+）从摄像头帧算出同样三个量 —— 本脚本只
// 把它们换成按帧预设值，MealMonitor 的编排路径与将来一致。
struct ScriptFrame {
    double motion_ratio = 0.0;
    std::vector<std::string> faces;   // 在场者姓名；不在档案库的视为未知人脸
    std::vector<prosophor::nr::domain::FoodItem> foods;
};

// 会话脚本：demo 循环每 Tick 前推进 frame_index，各 Fake 端口同步读当前帧
// —— 与真实实现"同一帧走完 感知→场景→菜品"的时序一致。
struct SessionScript {
    const std::vector<ScriptFrame>& frames;
    int frame_index = 0;
    const ScriptFrame& cur() const {
        return frames[static_cast<size_t>(frame_index)];
    }
};

// 人脸识别端口 Fake：按脚本名单查档案库 —— 产出（tags/禁忌）来自建档档案，
// 与真实实现（检测→特征→find_match 命中档案）的 FaceObservation 同构。
// 首次见到成员/未知人脸时打印识别轨迹（叙事用途；真实实现不打逐帧日志）。
class ScriptedVisionPort : public prosophor::nr::app::FaceRecognitionPort {
public:
    ScriptedVisionPort(prosophor::nr::domain::MemberRepository& repo,
                       const SessionScript& script)
        : repo_(&repo), script_(&script) {}

    std::vector<prosophor::nr::app::FaceObservation> detect_and_recognize(
        prosophor::nr::app::FrameRef, prosophor::nr::domain::MemberRepository&,
        double, int) override {
        std::vector<prosophor::nr::app::FaceObservation> out;
        int unknown_count = 0;
        for (const auto& name : script_->cur().faces) {
            prosophor::nr::app::FaceObservation obs;
            const auto m = repo_->get_member(name);   // 档案命中 = 认识
            if (m.has_value()) {
                obs.name = m->name;
                obs.similarity = 0.90 - 0.03 * out.size();
                obs.health_tags = m->profile.health_tags;
                obs.taboo_foods = m->profile.taboo_foods;
                if (announced_.insert(name).second) {
                    LOG_INFO("  t={:3}  [识别] {}（档案命中，相似度 {:.2f}）", script_->frame_index, name.c_str(), obs.similarity);
                }
            } else {
                // 未知人脸：能"看见人"，但身份个性化（禁忌按人）无法生效
                ++unknown_count;
            }
            out.push_back(std::move(obs));
        }
        if (unknown_count > 0 && !announced_unknown_) {
            announced_unknown_ = true;
            LOG_INFO("  t={:3}  [识别] 未知人脸 ×{}（未建档，不参与规则比对）", script_->frame_index, unknown_count);
        }
        return out;
    }
    void release() override {}

private:
    prosophor::nr::domain::MemberRepository* repo_;
    const SessionScript* script_;
    std::set<std::string> announced_;
    bool announced_unknown_ = false;
};

// 场景监控端口 Fake：运动量取脚本值，状态机用真实领域 FSM —— 确定性核心
// 本身无 Fake。阶段 2 的 infrastructure 实现 = 本类 + 帧差算法读真实帧。
class ScriptedSceneMonitorPort : public prosophor::nr::app::SceneMonitorPort {
public:
    explicit ScriptedSceneMonitorPort(const SessionScript& script)
        : script_(&script) {}

    std::string update(prosophor::nr::app::FrameRef, bool has_faces) override {
        const auto& frame = script_->cur();
        fsm_.update({frame.motion_ratio, has_faces});
        return std::string(fsm_.label());
    }
    void force(std::string_view label) override {
        if (const auto s = prosophor::nr::domain::scene_from_label(label)) {
            fsm_.force(*s);
        }
    }
    // 运动档位按 ports.h 约定（high/low/mid）；本 demo 未接 LLM 复核轨，仅备忘
    std::string motion_level() const override {
        const double m = script_->cur().motion_ratio;
        using prosophor::nr::domain::SceneStateMachine;
        if (m > SceneStateMachine::MOTION_HIGH) return "high";
        if (m < SceneStateMachine::MOTION_LOW) return "low";
        return "mid";
    }
    // 会话统计：真实 FSM 的转移次数（demo 收尾展示）
    int transitions() const { return fsm_.transitions(); }

private:
    const SessionScript* script_;
    prosophor::nr::domain::SceneStateMachine fsm_;
};

// 菜品检测端口 Fake（Phase 3 由 aidlite/qwen3vl 实现同名端口）
class ScriptedFoodPort : public prosophor::nr::app::FoodDetectorPort {
public:
    explicit ScriptedFoodPort(const SessionScript& script) : script_(&script) {}
    std::vector<prosophor::nr::domain::FoodItem> detect_domain(
        prosophor::nr::app::FrameRef) override {
        return script_->cur().foods;
    }

private:
    const SessionScript* script_;
};

// 整段文本按行加前缀打印（对齐 demo 输出层级，如 JSON 档案缩进）
void PrintPrefixedLines(const std::string& text, const std::string& prefix) {
    std::string line;
    for (const char ch : text) {
        if (ch == '\n') {
            LOG_INFO("{}{}", prefix.c_str(), line.c_str());
            line.clear();
        } else {
            line += ch;
        }
    }
    if (!line.empty()) LOG_INFO("{}{}", prefix.c_str(), line.c_str());
}

// 集合元素以中文顿号连接（demo 输出用）
std::string JoinNames(const std::set<std::string>& names) {
    std::string out;
    for (const auto& n : names) {
        if (!out.empty()) out += "、";
        out += n;
    }
    return out;
}

// 菜品名以中文顿号连接（保序，与识别输出一致）
std::string JoinFoodNames(const std::vector<prosophor::nr::domain::FoodItem>& foods) {
    std::string out;
    for (const auto& f : foods) {
        if (!out.empty()) out += "、";
        out += f.name;
    }
    return out;
}

// demo 成员档案库：/tmp 下固定目录，每次运行清空重建（自检幂等）。
// 真实产品存储路径待主程序装配时定（config/.prosophor 或用户数据目录）。
std::string PrepareMemberStoreDir() {
    const std::string dir = "/tmp/prosophor/nutribot_demo_members";
    for (const auto& f : prosophor::ListDir(dir, ".json")) {
        prosophor::RemoveFile(prosophor::JoinPath(dir, f));
    }
    prosophor::EnsureDirectory(dir);
    return dir;
}

// 用餐故事脚本（230 tick）：端菜走动（备餐）→ 成员入座、上齐菜（用餐）
// → 离席静默（餐后）→ 收桌（空桌待机）。菜品 12 tick 起上；70 tick 新增
// 花生酱拌面 —— 命中小明"花生"禁忌，制造用餐中的第二次提醒。
std::vector<ScriptFrame> BuildMealScript() {
    static const std::vector<prosophor::nr::domain::FoodItem> kDishes = {
        {"f1", "牛奶"}, {"f2", "白灼虾"}, {"f3", "清蒸鲈鱼"},
        {"f4", "奶油蛋糕"}, {"f5", "清炒时蔬"}, {"f6", "花生酱拌面"},
    };
    std::vector<ScriptFrame> script(230);
    for (size_t t = 0; t < script.size(); ++t) {
        ScriptFrame& f = script[t];
        if (t < 30)              f.motion_ratio = 0.25;   // 端菜走动（high）
        else if (t < 40)         f.motion_ratio = 0.03;   // 上菜收尾（mid）
        else if (t < 130)        f.motion_ratio = 0.02;   // 进食小动作（low）
        else                     f.motion_ratio = 0.003;  // 离席静默（静止）
        if (t >= 40 && t < 130)  f.faces = {"小明", "王奶奶", "同学"};
        const int dish_count = t < 12 ? 0 : (t < 70 ? 5 : 6);
        f.foods.assign(kDishes.begin(), kDishes.begin() + dish_count);
    }
    return script;
}

// [3] 注册用例：对话式画像采集 → 成员档案落盘（确定性核心 + 真实仓储）。
//   两位脚本化新成员建档；识别/规则所需的档案字段由 [4] 复用本库读取。
void DemoRegistrationQuiz(prosophor::nr::infra::JsonMemberRepository& repo) {
    const std::vector<RegistrationCase> cases = {
        {"小明", {"小明", "6", "男", "牛奶、花生", "虾", "少盐", "清淡、甜食",
                  "儿童、乳糖不耐受"}},
        {"王奶奶", {"王奶奶", "68", "女", "蛋糕、肥肉", "没有", "低糖、少盐",
                    "软烂、清淡", "2型糖尿病、老年人"}},
    };
    for (const auto& c : cases) {
        LOG_INFO("  -- 新成员 {}（8 问对话建档）--", c.name);
        ScriptedDialoguePort dialogue(c.answers);
        const auto member =
            prosophor::nr::app::RunQuizCli(repo, std::nullopt, dialogue);
        if (!member.has_value()) {
            LOG_INFO("    注册中止（未取得姓名）");
            continue;
        }
        // 读回落盘档案并打印：证明 JSON 全字段持久化（member_id/registered_at 自动生成）
        const auto back = repo.get_member(member->name);
        if (!back.has_value()) {
            LOG_INFO("    !! 读回失败");
            continue;
        }
        const std::string json =
            prosophor::nr::infra::JsonMemberRepository::member_to_json(*back);
        LOG_INFO("    落盘档案读回（{}.json）:", back->name.c_str());
        PrintPrefixedLines(json, "      ");
    }
    // 档案库成员一览（[4] 识别直接查询本库）
    const auto all = repo.list_members();
    LOG_INFO("  档案库现有 {} 位成员:", all.size());
    for (size_t i = 0; i < all.size(); ++i) {
        LOG_INFO(" {}{}", all[i].name.c_str(),
                    (i + 1 < all.size()) ? "、" : "");
    }
    LOG_INFO("");
}

// [4] 用餐监控用例（确定性核心端到端编排）：
//   MealMonitor 每 Tick：感知 → FSM → 规则比对 → 结构化场景包；
//   状态转移与提醒只打印"变化点"（提醒跨帧去重 —— 播报限频语义，真队列在 Phase 4）。
//   餐后触发小结打印；全周期回到空桌待机收尾。
void DemoMealMonitorSession(prosophor::nr::infra::JsonMemberRepository& repo) {
    const std::vector<ScriptFrame> frames = BuildMealScript();
    SessionScript script{frames};
    ScriptedVisionPort vision(repo, script);
    ScriptedSceneMonitorPort scene(script);
    ScriptedFoodPort food(script);
    // rules 走内置默认引擎（营养比对 Phase 3 空实现）；judge 未注入 = 纯状态机确定轨
    // （Phase 5 LLM 复核以后接 SceneJudgingPort，[5] 的 aidgen 即其后端候选）
    prosophor::nr::app::MealMonitor monitor(&vision, &repo, nullptr, &scene, &food);

    std::set<std::string> members_seen, foods_seen;
    bool saw_unknown = false;
    std::set<std::string> event_keys;
    std::vector<prosophor::nr::domain::ReminderEvent> reminders;
    std::string prev_state;

    LOG_INFO("  -- 用餐观察 {} tick（感知为脚本 Fake；叙事轨迹如下）--", frames.size());
    for (size_t t = 0; t < frames.size(); ++t) {
        script.frame_index = static_cast<int>(t);
        // FrameRef：Fake 不解引用，阶段 2 起替换为真实摄像头帧
        const auto pkg = monitor.Tick(nullptr);

        for (const auto& m : pkg.members) {
            if (m.name.empty()) {
                saw_unknown = true;
            } else {
                members_seen.insert(m.name);
            }
        }
        for (const auto& f : pkg.foods) foods_seen.insert(f.name);

        // 提醒首报（同人同菜跨帧去重）
        for (const auto& e : pkg.events) {
            const std::string key = e.member_name + "|" + e.food;
            if (event_keys.insert(key).second) {
                reminders.push_back(e);
                LOG_INFO("  t={:3}  [禁忌提醒] {}", t, e.message.c_str());
            }
        }

        if (pkg.scene_state != prev_state) {
            if (prev_state.empty()) {
                LOG_INFO("  t={:3}  初始状态 {}", t, pkg.scene_state.c_str());
            } else {
                LOG_INFO("  t={:3}  状态 {} → {}", t, prev_state.c_str(),
                            pkg.scene_state.c_str());
            }
            // 餐后触发小结：真实系统由 Agent 把场景包话术化并 TTS（Phase 6）
            if (pkg.scene_state == "餐后") {
                LOG_INFO("       ↓ 餐后小结（结构化；确定性核心产出）");
                LOG_INFO("        在场成员（累计识别）: {}{}", JoinNames(members_seen).c_str(),
                            saw_unknown ? " ＋未建档人脸（不参与规则比对）" : "");
                LOG_INFO("        累计菜品: {}", JoinNames(foods_seen).c_str());
                LOG_INFO("        禁忌/过敏提醒 {} 条（首报去重）:", reminders.size());
                for (const auto& e : reminders) {
                    LOG_INFO("          · [{}] {}", e.kind.c_str(),
                                e.message.c_str());
                }
            }
            prev_state = pkg.scene_state;
        }
    }
    LOG_INFO("  -- 全周期完成：空桌待机 → 备餐中 → 用餐中 → 餐后 → 空桌待机");
    LOG_INFO("     状态转移 {} 次（FSM 帧计数消抖）; 提醒 {} 条", scene.transitions(), reminders.size());
}

// [5] AgentEngine 会话自检：CreateSession(nutribot) + SendUserMessage
// LLM 调用一律经 AgentEngine（角色/路由/会话体系），不直连 provider。
// SendUserMessage 是同步接口：阻塞至整轮结束并返回助手回复 —— 回调仅用于
// 实时打印流式过程，无需 done 标志协调。
void DemoEngineAgent() {
    auto& engine = prosophor::AgentEngine::GetInstance();

    // engine.AddOutputCallback(
    //     [](const std::string& /*session*/, const std::string& /*role*/,
    //             prosophor::AgentRuntimeState state, const std::string& state_msg,
    //             const std::optional<prosophor::MessageSchema>& /*reply*/,
    //             const std::string& delta) {
    //         // 本回调是流式交互显示：增量要逐段续写、不换行 —— 走 std::cout 而非
    //         // 日志。日志每条自带时间戳与换行，会把打字机效果打散成一行一个 token。
    //         switch (state) {
    //             case prosophor::AgentRuntimeState::BEGINNING:
    //                 std::cout << "  ● [开始] " << state_msg << std::endl;
    //                 break;
    //             case prosophor::AgentRuntimeState::STREAM_THINKING_START:
    //                 std::cout << "  ○ [思考] " << std::flush;   // 后续增量续在同一行
    //                 break;
    //             case prosophor::AgentRuntimeState::STREAM_THINKING:
    //                 std::cout << delta << std::flush;
    //                 break;
    //             case prosophor::AgentRuntimeState::STREAM_THINKING_END:
    //                 std::cout << " [思考结束]" << std::endl;
    //                 break;
    //             case prosophor::AgentRuntimeState::STREAM_CONTENT_START:
    //                 std::cout << "  ● [回答] " << std::flush;
    //                 break;
    //             case prosophor::AgentRuntimeState::STREAM_CONTENT_TYPING:
    //                 std::cout << delta << std::flush;
    //                 break;
    //             case prosophor::AgentRuntimeState::STREAM_CONTENT_END:
    //                 std::cout << " [回答结束]" << std::endl;
    //                 break;
    //             case prosophor::AgentRuntimeState::EXECUTING_TOOL:
    //                 std::cout << "  ◇ [工具] " << state_msg << std::endl;
    //                 break;
    //             case prosophor::AgentRuntimeState::COMPLETE:
    //                 std::cout << std::endl;
    //                 break;
    //             case prosophor::AgentRuntimeState::STATE_ERROR:
    //                 std::cout << "  会话错误: " << state_msg << std::endl;
    //                 break;
    //             default:
    //                 break;
    //         }
    //     });

    const std::string session_id =
        engine.CreateSession("nutribot", "NutriBot 自检会话");
    LOG_INFO("  会话 {} 已创建（角色 nutribot，默认 LLM provider）", session_id.c_str());
    LOG_INFO("  >>> 你：请用一句话介绍你能做什么\n  <<< ");

    // 同步：返回即整轮结束，回复直接来自返回值（上面的回调只管实时流式打印）
    const std::string reply = engine.SendUserMessage(session_id, "请用一句话介绍你能做什么");
    if (reply.empty()) {
        LOG_INFO("  [会话完成]（无回复内容）");
    } else {
        LOG_INFO("  [完整回复] {}", reply.c_str());
    }
}

// [5b] VL 图文自检：复用 [5] 会话所用的同一 provider 实例。
// aidgen Context 进程内唯一 —— 不能再装配第二个模型，故此处只做能力判别与提问：
//   配置无 vlm_model 段  -> 说明当前装配的是纯文本模型，提示如何换 VL 配置
//   是 VL 但编码器未就绪  -> 报出 GetLoadError()（板上为 aidlite 版本门）
//   两者齐备            -> 发一条真实图文请求
void DemoVlImageCheck() {
    // 测试图随仓库放在 tests/nutribot/assets（路径相对 cwd，同 config/.prosophor 惯例）
    const std::string image = "tests/nutribot/assets/vl_demo.jpg";
    auto provider = prosophor::LlmProviderRouter::GetInstance()
                        .GetProviderByName("aidgen");
    auto* vl = dynamic_cast<prosophor::AidGenProvider*>(provider.get());
    if (vl == nullptr || !vl->IsLoaded()) {
        LOG_INFO("  （跳过: aidgen provider 未装配/未装载）");
        return;
    }
    if (!vl->HasVisionConfig()) {
        LOG_INFO("  （跳过: 当前装配为纯文本模型 —— 要跑图文，把 settings.json 里\n   llm_providers.aidgen.config_file 指向含 vlm_model 段的 VL 配置，\n   例如 ~/aidmlm/qnn236_qcs8550_cl2048/aidgen_config.json）");
        return;
    }
    if (!prosophor::FileExists(image)) {
        LOG_INFO("  （跳过: 缺少演示图 {}）", image.c_str());
        return;
    }

    prosophor::ChatRequest req;
    prosophor::MessageSchema user("user", "请用中文简要描述这张图片的内容");
    user.AddImageContent(image);
    req.messages.push_back(std::move(user));
    const auto resp = vl->Chat(req);
    if (!resp.error_msg.empty()) {
        LOG_INFO("  图文自检失败: {}", resp.error_msg.c_str());
        return;
    }
    LOG_INFO("  [完整回复] {}", resp.content_text.c_str());
}

// [5c] 多轮随时给图自检：走 agent engine（非直连 provider）。
// 用户中途给图/换图 —— 图随它跟的那条消息走（瞬时附件位），不泄漏到后续轮次。
// 第 2 轮给的是语义可区分的裁图（狗，无人物）：若后续轮次答成别的，说明模型看的
// 不是这张图 —— 这是本条用例要抓的失败模式。
void DemoMultiTurnVision() {
    // 两张裁图由 vl_demo.jpg 沿中线竖切而来：左半只有狗、右半只有人。
    // 内容互斥是刻意的 —— 答错即可判定"模型看的不是当轮那张图"。
    const std::string dog_image    = "tests/nutribot/assets/vl_dog.jpg";
    const std::string person_image = "tests/nutribot/assets/vl_person.jpg";
    if (!prosophor::FileExists(dog_image) || !prosophor::FileExists(person_image)) {
        LOG_INFO("  （跳过: 缺少测试裁图 {} / {}）", dog_image, person_image);
        return;
    }

    // 纯文本装配下不支持带图：此处与 [5b] 同样先判别，避免把带图消息发进会话
    // 污染后续轮次（模型收到有视觉界标却无视觉输入的提示词会答非所问）
    auto provider = prosophor::LlmProviderRouter::GetInstance().GetProviderByName("aidgen");
    auto* vl = dynamic_cast<prosophor::AidGenProvider*>(provider.get());
    if (vl == nullptr || !vl->IsLoaded() || !vl->HasVisionConfig()) {
        LOG_INFO("  （跳过: 当前装配非多模态模型）");
        return;
    }

    auto& engine = prosophor::AgentEngine::GetInstance();
    const std::string session_id = engine.CreateSession("nutribot", "多轮给图自检");

    // 空路径即"本轮不带图" —— 路径本无空值语义，不必再套 optional 或指针
    struct VisionTurn {
        const char* question;
        std::string image_path;
    };
    const std::vector<VisionTurn> turns = {
        {"请用一句话说明你能做什么",              ""},
        {"这张图里有什么动物？",                  dog_image},
        {"它旁边还有别的东西吗？",                ""},
        {"换成这张图，里面的人穿什么颜色的衣服？",  person_image},
    };

    for (size_t i = 0; i < turns.size(); ++i) {
        const auto& turn = turns[i];
        const bool carries_image = !turn.image_path.empty();
        LOG_INFO("  ── 第 {} 轮{} ──", i + 1, carries_image ? "（带图）" : "（纯文本）");
        LOG_INFO("  >>> 你：{}", turn.question);
        const std::string reply = carries_image
            ? engine.SendUserMessage(session_id, turn.question,
                                     std::vector<std::string>{turn.image_path})
            : engine.SendUserMessage(session_id, turn.question);
        LOG_INFO("  [回复] {}", reply.empty() ? "(空)" : reply);
    }
}

// [6] 看菜自检（Phase 3）：VL 看帧 -> 菜品列表 -> MealMonitor -> 规则引擎。
// 直连 provider（同 [5b]）—— 感知调用不是对话：问的是"图里有什么菜"、答的是机器
// 要解析的 JSON，角色人格 prompt 在此只会帮倒忙，也不该进对话会话历史。
void DemoFoodRecognition(prosophor::nr::infra::JsonMemberRepository& repo) {
    // 菜图随仓库不带（拍图命令见下方跳过提示）；无图时本段跳过，链路仍由单测端到端覆盖
    const std::string image = "tests/nutribot/assets/vl_food_table.jpg";
    if (!prosophor::FileExists(image)) {
        LOG_INFO("  （跳过: 缺少菜图 {} —— 可拍一张:\n   ffmpeg -f v4l2 -i /dev/video0 -frames:v 1 -y {}）",
                 image.c_str(), image.c_str());
        return;
    }
    auto provider = prosophor::LlmProviderRouter::GetInstance()
                        .GetProviderByName("aidgen");
    auto* vl = dynamic_cast<prosophor::AidGenProvider*>(provider.get());
    if (vl == nullptr || !vl->IsLoaded()) {
        LOG_INFO("  （跳过: aidgen provider 未装配/未装载）");
        return;
    }
    if (!vl->HasVisionConfig()) {
        LOG_INFO("  （跳过: 当前装配为纯文本模型，菜品识别需要含 vlm_model 段的 VL 配置）");
        return;
    }

    // 后端：图片 + 提问 -> 回复文本（装配同 [5b] 的 ChatRequest 图文请求）。
    // 计数供节流/缓存自证：模型调用次数必须与 detect_domain 次数解耦。
    // last_reply 供解析失败时回看模型原话 —— 检测器只交出解析结果，证据留在装配层。
    int model_calls = 0;
    std::string last_reply;
    prosophor::nr::app::VisionQueryBackend backend =
        [vl, &model_calls, &last_reply](const std::string& image_path,
                                        const std::string& question)
        -> std::optional<std::string> {
        prosophor::ChatRequest req;
        prosophor::MessageSchema user("user", question);
        user.AddImageContent(image_path);
        req.messages.push_back(std::move(user));
        ++model_calls;
        const auto resp = vl->Chat(req);
        if (!resp.error_msg.empty()) {
            LOG_INFO("  [后端错误] {}", resp.error_msg.c_str());
            return std::nullopt;
        }
        last_reply = resp.content_text;
        return resp.content_text;
    };

    // min_interval=0：本节要验的是"同帧缓存"，不是节流窗口（真实帧流用默认 3s）
    prosophor::nr::app::VlFoodDetector detector(backend, /*min_interval_seconds=*/0.0);
    prosophor::nr::app::ImageFrame frame{image};

    LOG_INFO("  -- 识别（真调模型）--");
    // 诊断：同一张图先问开放式问题 —— 模型若答得出内容，说明图进了模型，问题在提问；
    // 若答"看不到图"，则是视觉链路没接上
    if (const auto probe = backend(image, "请用中文一句话说明这张图片里有什么")) {
        LOG_INFO("  [描述探针] {}", probe->c_str());
    }
    const auto foods = detector.detect_domain(&frame);
    LOG_INFO("  [菜品] {}",
             foods.empty() ? "（模型未看到菜）" : JoinFoodNames(foods).c_str());
    if (foods.empty() && !last_reply.empty()) {
        LOG_INFO("  [模型原话] {}", last_reply.c_str());   // 解析失败时回看原话
    }

    LOG_INFO("  -- 同帧连调 3 次（帧未变，不应再调模型）--");
    for (int i = 0; i < 3; ++i) detector.detect_domain(&frame);
    LOG_INFO("  模型调用累计 {} 次 / detect_domain 调用 4 次",
             detector.query_count());

    // 走完整管道：人脸由脚本注入（身份识别不是本节题目），菜品来自 VL 真实识别
    std::vector<ScriptFrame> frames(1);
    frames[0].faces = {"小明"};
    SessionScript script{frames};
    ScriptedVisionPort vision(repo, script);
    prosophor::nr::app::MealMonitor monitor(&vision, &repo, nullptr, nullptr,
                                            &detector);

    LOG_INFO("  -- 走 MealMonitor（人脸脚本注入\"小明\"，菜品来自 VL）--");
    const auto pkg = monitor.Tick(&frame);
    LOG_INFO("  [场景包] 菜品: {}",
             pkg.foods.empty() ? "（无）" : JoinFoodNames(pkg.foods).c_str());
    if (pkg.events.empty()) {
        LOG_INFO("  [提醒] 无（桌上菜品未命中在座成员禁忌）");
    }
    for (const auto& e : pkg.events) {
        LOG_INFO("  [提醒] {}", e.message.c_str());
    }
    LOG_INFO("  模型调用累计 {} 次（管道内同样命中同帧缓存）", detector.query_count());

    // 多菜样本：单菜只证明"能认"，多菜才证明"能认全" —— 真实餐桌必然多菜。
    // 素材：食堂餐盘公开数据集 img_100，Pascal VOC 真值 5 类（炸鸡/空心菜/豆芽/
    // 香肠/白菜），另有真值未收录的豆腐。真值供人工核对召回，不参与程序判定。
    const std::string multi_image = "tests/nutribot/assets/vl_food_table.jpg";
    if (prosophor::FileExists(multi_image)) {
        LOG_INFO("  -- 多菜样本（真值 5 菜 + 豆腐）--");
        const prosophor::nr::app::ImageFrame multi_frame{multi_image};
        const auto multi_foods = detector.detect_domain(&multi_frame);
        LOG_INFO("  [菜品] {}（共 {} 道）",
                 multi_foods.empty() ? "（无）" : JoinFoodNames(multi_foods).c_str(),
                 multi_foods.size());
    }

    // 逐菜基准：dish 目录是切好的单菜图 + 人工标注（labels.tsv）。
    // 每张图独立走一次 detect_domain，答案进日志，由 scripts 侧比对标注算准确率。
    // 换 provider 配置（换模型/换分辨率）后重跑本段即可得到新数字。
    {
        const std::string dir = "tests/nutribot/assets/dishes";
        std::set<std::string> files;
        for (const auto& f : prosophor::ListDir(dir, ".jpg")) {
            if (f.find("copy_of") != std::string::npos) continue;   // 调试残留
            files.insert(f);
        }
        if (!files.empty()) {
            LOG_INFO("  -- 逐菜批量识别（{} 张，单菜图）--", files.size());
            int blank = 0;
            for (const auto& f : files) {
                const prosophor::nr::app::ImageFrame dish_frame{
                    prosophor::JoinPath(dir, f)};
                const auto dish_foods = detector.detect_domain(&dish_frame);
                if (dish_foods.empty()) ++blank;
                LOG_INFO("  {}  {}", f.c_str(),
                         dish_foods.empty() ? "（无）" : JoinFoodNames(dish_foods).c_str());
                // 解析为空时回看模型原话：区分"模型没输出"与"输出被解析器过滤"
                // （回吐守卫会丢弃 prompt 里出现过的字面，若模型答"餐桌上菜品"这类改述，
                //   结果为空但并非模型沉默 —— 少了这行会把它误记成识别失败）
                if (dish_foods.empty()) {
                    LOG_INFO("      raw={}", last_reply.empty() ? "(空)" : last_reply.c_str());
                }
            }
            LOG_INFO("  未识别出菜品 {} / {} 张", blank, files.size());
        }
    }

    // 负样本：非餐桌图应识别不出菜品（防模型对着什么都报菜名）。
    // 放最后：换图会刷新帧缓存，不影响上面的缓存自证。
    const std::string negative_image = "tests/nutribot/assets/vl_demo.jpg";
    if (prosophor::FileExists(negative_image)) {
        LOG_INFO("  -- 负样本 {}（非餐桌图，应输出空数组）--", negative_image.c_str());
        const prosophor::nr::app::ImageFrame negative_frame{negative_image};
        const auto negative_foods = detector.detect_domain(&negative_frame);
        LOG_INFO("  [菜品] {}（模型调用累计 {} 次）",
                 negative_foods.empty() ? "（无 —— 未误报）"
                                        : JoinFoodNames(negative_foods).c_str(),
                 detector.query_count());
    }
}

}  // namespace

int NutriBotApp::Run() {
    LOG_INFO("nutribot (C++ demo)");
    LOG_INFO("[1] 场景状态机轨迹（确定性核心）:");
    DemoSceneMachine();
    LOG_INFO("[2] 禁忌规则引擎（确定性核心）:");
    DemoRuleEngine();

    // 注册与监控共用一份真实 JSON 档案库（/tmp 幂等重建）
    const std::string members_dir = PrepareMemberStoreDir();
    prosophor::nr::infra::JsonMemberRepository members(members_dir);

    LOG_INFO("[3] 注册用例: 对话式画像采集 → 档案落盘（{}）:", members_dir.c_str());
    DemoRegistrationQuiz(members);
    LOG_INFO("[4] 用餐监控用例: MealMonitor + Fake 感知（确定性核心端到端）:");
    DemoMealMonitorSession(members);
    LOG_INFO("[5] LLM 会话自检（nutribot 角色 + aidgen/aidgen-vl provider，NPU）:");
    DemoEngineAgent();
    LOG_INFO("[5b] VL 图文自检（aidgen-vl 装配下, 同一 Context 实例）:");
    DemoVlImageCheck();
    LOG_INFO("[5c] 多轮随时给图自检（agent engine，图随消息走，含中途换图）:");
    DemoMultiTurnVision();
    LOG_INFO("[6] 看菜自检（VL 看帧 -> 菜品 -> MealMonitor -> 规则引擎）:");
    DemoFoodRecognition(members);
    return 0;
}

}  // namespace prosophor::nr::nutribot
