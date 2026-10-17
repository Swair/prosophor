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
#include <ctime>
#include <deque>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "common/file_utils.h"
#include "config/config.h"
#include "core/agent_types.h"
#include "core/messages_schema.h"
#include "platform/platform.h"

#include "agent_engine.h"
#include "audio_io.h"
#include "camera_source.h"
#include "diner_context.h"
#include "dish_nutrition.h"
#include "events.h"
#include "food.h"
#include "meal_journal.h"
#include "meal_monitor.h"
#include "meal_schedule.h"
#include "member.h"
#include "member_repository.h"
#include "ports.h"
#include "proactive_announcer.h"
#include "quiz_cli.h"
#include "rule_engine.h"
#include "scene.h"
#include "scene_monitor.h"
#include "vision_stub.h"
#include "voice_session.h"
#include "vl_food_detector.h"

#include "providers/asr/aidvoice_asr_provider.h"
#include "providers/llm/aidgen_provider.h"
#include "providers/llm/llm_provider.h"
#include "providers/provider_router/asr_provider_router.h"
#include "providers/provider_router/llm_provider_router.h"
#include "providers/provider_router/tts_provider_router.h"
#include "providers/tts/aidvoice_tts_provider.h"

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

    std::string update(prosophor::nr::app::FrameRef, bool has_faces,
                       bool has_foods) override {
        const auto& frame = script_->cur();
        fsm_.update({frame.motion_ratio, has_faces, has_foods});
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

// 营养库路径：配置非空 -> 用它；否则 <BaseDir>/nutribot_dishes.json；再退回
// 仓库相对路径（cwd=仓库根时命中，与 config/.prosophor 惯例同构）
std::string ResolveNutritionFile(const prosophor::NutribotConfig& cfg) {
    if (!cfg.nutrition_file.empty()) {
        return prosophor::ExpandHome(cfg.nutrition_file);
    }
    const std::string base = prosophor::JoinPath(
        prosophor::ProsophorConfig::BaseDir().string(), "nutribot_dishes.json");
    if (prosophor::FileExists(base)) return base;
    return "config/.prosophor/nutribot_dishes.json";
}

// 逐菜基准的人工标注（labels.tsv：文件名 -> 菜名真值；# 开头为注释）
std::map<std::string, std::string> LoadDishLabels() {
    std::map<std::string, std::string> labels;
    const std::string path = "tests/nutribot/assets/dishes/labels.tsv";
    if (!prosophor::FileExists(path)) return labels;
    const auto text = prosophor::ReadFile(path);
    if (!text.has_value()) return labels;
    std::string line;
    for (const char ch : *text) {
        if (ch != '\n') { line += ch; continue; }
        const size_t tab = line.find('\t');
        if (!line.empty() && line[0] != '#' && tab != std::string::npos) {
            labels.emplace(line.substr(0, tab), line.substr(tab + 1));
        }
        line.clear();
    }
    return labels;
}

// 两道菜名是否指同一道（评测口径）：精确，或含头含尾的包含（两侧≥2 字符）
// —— 复用闭集匹配的规则，避免评测口径与产品口径分叉
bool SameDish(const std::string& a, const std::string& b) {
    return prosophor::nr::app::MatchCandidateName(a, {b}).has_value();
}

// [7] 营养库自检：加载真实数据文件 -> 覆盖率（对识别基准人工标注）-> 样例热量
void DemoNutrition() {
    const std::string path =
        ResolveNutritionFile(prosophor::ProsophorConfig::GetInstance().nutribot);
    std::string error;
    auto table = prosophor::nr::app::DishNutritionTable::LoadFromFile(path, error);
    if (!table.has_value()) {
        LOG_INFO("  （跳过: {}）", error.c_str());
        return;
    }
    LOG_INFO("  营养库 {} 道菜（跳过坏条目 {}）：{}", table->size(),
             table->skipped_entries(), path.c_str());

    // 覆盖率：识别基准标注（labels.tsv，人工判读的菜名）里有多少能查到营养
    std::set<std::string> benchmark;
    for (const auto& [file, truth] : LoadDishLabels()) {
        benchmark.insert(truth);
    }
    if (!benchmark.empty()) {
        std::vector<std::string> missed;
        for (const auto& name : benchmark) {
            if (!table->Match(name).has_value()) missed.push_back(name);
        }
        LOG_INFO("  识别基准菜名覆盖：{}/{}", benchmark.size() - missed.size(),
                 benchmark.size());
        if (!missed.empty()) {
            const std::set<std::string> missed_set(missed.begin(), missed.end());
            LOG_INFO("  未覆盖：{}", JoinNames(missed_set).c_str());
        }
    }

    // 样例：走一遍"菜名 -> 每份营养"，附带来源备注
    const std::vector<std::string> samples = {"红烧肉", "番茄蛋花汤", "没见过的菜"};
    for (const auto& name : samples) {
        const auto match = table->Match(name);
        if (match.has_value()) {
            LOG_INFO("  {} -> 约 {} 千卡/份（钠 {} 毫克{}）", name.c_str(),
                     static_cast<int>(match->per_serving.energy_kcal),
                     static_cast<int>(match->per_serving.sodium_mg),
                     match->source.empty() ? "" : "，" + match->source);
        } else {
            LOG_INFO("  {} -> （未知，播报将计为\"没见过\"）", name.c_str());
        }
    }
}

// [8] 机器人控制自检：注册表按名执行 robot_* 工具（与主循环同一条执行路径），
// 移动用低速短脉冲——自检也会让机器人走一步，跑之前确认场地安全
void DemoRobotControl() {
    auto& registry = prosophor::ToolRegistry::GetInstance();
    const auto& cfg = prosophor::ProsophorConfig::GetInstance().robot;
    if (!cfg.enabled) {
        LOG_INFO("  （跳过: robot.control_enabled=false）");
        return;
    }

    LOG_INFO("  状态: {}", registry.ExecuteTool("robot_status", {}).c_str());
    LOG_INFO("  移动: {}",
             registry.ExecuteTool("robot_move", {{"direction", "forward"}, {"speed", 30}})
                 .c_str());
    LOG_INFO("  舵机: {}",
             registry.ExecuteTool("robot_servo", {{"servo_id", 6}, {"angle", 30}}).c_str());
    LOG_INFO("  表情: {}",
             registry.ExecuteTool("robot_face", {{"face", "happy"}}).c_str());
    LOG_INFO("  非法方向（应被参数校验拦下）: {}",
             registry.ExecuteTool("robot_move", {{"direction", "fly"}}).c_str());
}

// 动作命令判定（演示向）：小模型在历史里看到"同一指令 + 已完成的文字回复"会
// 照抄那条回复而不是重新执行（实测：连说两遍"做个开心的表情"，第二遍只回
// "好的，我已经开心地笑了。"）。消息前贴短标记把注意力拉回工具调用 ——
// 远处 soul 里的长指令压不住这种近邻模仿。
bool IsActionCommand(const std::string& text) {
    static const std::vector<std::string> action_words = {
        "爪子", "机械爪", "手臂", "表情", "移动", "前进", "后退",
        "走一步", "转身", "状态", "张开", "闭上", "合上", "握"};
    for (const auto& word : action_words) {
        if (text.find(word) != std::string::npos) return true;
    }
    return false;
}

// 环境噪音过滤：ASR 会把电视/人声碎片转成 "."、"The."、"。" 这类句子，送进
// LLM 会污染对话历史并把小上下文灌满（实测 8 分钟 35 句全是噪音，prompt 涨到
// 3000+ 后模型只会吐 EOS）。口语指令以中文为主：至少两个汉字才算有效话轮。
bool IsMeaningfulUtterance(const std::string& text) {
    int cjk = 0;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if ((byte & 0xC0) == 0x80) continue;   // 续接字节
        if (byte >= 0xE4 && byte <= 0xE9) ++cjk;   // UTF-8 常用汉字首字节
    }
    return cjk >= 2;
}

// 定向话轮判定：只有提问或命令才值得送进 LLM。环境里的电视/有声书多是陈述句，
// 送进去只会得到（沉默）并污染历史 —— 模型会从自己的历史里模仿"沉默"模式，
// 连真问题也开始沉默（实测 4B：连续两个营养问题重问两次仍全沉默）。
bool IsDirectedUtterance(const std::string& text) {
    if (IsActionCommand(text)) return true;
    static const std::vector<std::string> question_markers = {
        "吗", "？", "?", "什么", "怎么", "哪", "推荐", "可以",
        "多少", "是否", "能不能", "要不要", "怎么样"};
    for (const auto& marker : question_markers) {
        if (text.find(marker) != std::string::npos) return true;
    }
    return false;
}

// 电机是锁存语义：一次 move 指令后固件保持目标速度，只有显式 stop 才停。
// 上一实例若在"发方向 → 睡 1 秒 → 发 stop"之间崩溃，stop 就永远没机会发出 ——
// 启动与退出时各清一次，防止带着锁存目标无人看管地跑。
void ClearLatchedMotion(const char* occasion) {
    if (!prosophor::ProsophorConfig::GetInstance().robot.enabled) return;
    auto& registry = prosophor::ToolRegistry::GetInstance();
    if (!registry.HasTool("robot_move")) return;
    const std::string result =
        registry.ExecuteTool("robot_move", {{"direction", "stop"}});
    LOG_INFO("robot: {}清运动目标 -> {}", occasion, result.c_str());
}

// 板上环境约束（2026-10-13 实测，装 aidvoice-sdk 后）：aidlite(qnn248) 必须先于
// aidgen 核心（QNN 2.36/Genie）初始化，否则视觉编码器的 QNN deviceCreate 直接失败
// （"QNN deviceCreate failed 14001"），[5b]/[5c]/[6] 的视觉段全灭 —— 实测先预热
// aidvoice 一次即恢复（五菜样本逐菜正确）。常驻路径天然满足（ASR/TTS 预热在前）；
// 自检路径无语音，故在 [5] 之前显式预热一次。失败不阻断：只告警，视觉段照常报错。
void PreloadAidliteBeforeAidgen() {
    // AgentEngine 首次构造会完成 TTS 路由器 Initialize（读配置）
    (void)prosophor::AgentEngine::GetInstance();
    auto provider = prosophor::TtsProviderRouter::GetInstance().GetProvider();
    if (auto* tts = dynamic_cast<prosophor::AidVoiceTtsProvider*>(provider.get())) {
        std::string error;
        if (!tts->WarmUp(error)) {
            LOG_WARN("nutribot: TTS 预热失败（{}），视觉编码器可能装载失败（aidlite 顺序约束）",
                     error.c_str());
        }
    } else if (provider == nullptr) {
        LOG_WARN("nutribot: TTS provider 不可用，视觉编码器可能装载失败（aidlite 顺序约束）");
    }
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
        LOG_INFO("  （跳过: 当前装配为纯文本模型 —— 要跑图文，把 settings.json 里\n   llm_providers.aidgen.config_file 指向含 vlm_model 段的 VL 配置，\n   例如 ~/models/qnn236_qcs8550_cl2048/aidgen_config.json）");
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

// aidgen 图文后端：图片 + 提问 -> 回复文本。感知调用直连 provider（同 [5b]）——
// 不是对话：问的是"图里有什么菜"、答的是机器要解析的 JSON，角色人格 prompt
// 只会帮倒忙，也不该进对话会话历史。model_calls/last_reply 供节流自证与解析
// 失败回看（可传 nullptr）；selftest [6] 与常驻感知共用本工厂。
prosophor::nr::app::VisionQueryBackend MakeAidgenVisionBackend(
    prosophor::AidGenProvider* vl, int* model_calls, std::string* last_reply) {
    return [vl, model_calls, last_reply](const std::string& image_path,
                                         const std::string& question)
               -> std::optional<std::string> {
        prosophor::ChatRequest req;
        prosophor::MessageSchema user("user", question);
        user.AddImageContent(image_path);
        req.messages.push_back(std::move(user));
        req.max_tokens = 128;   // 识别输出只是短 JSON 数组：限尾防小模型放飞（默认 8192）
        if (model_calls != nullptr) ++*model_calls;
        const auto resp = vl->Chat(req);
        if (!resp.error_msg.empty()) {
            LOG_INFO("  [后端错误] {}", resp.error_msg.c_str());
            return std::nullopt;
        }
        if (last_reply != nullptr) *last_reply = resp.content_text;
        return resp.content_text;
    };
}

// [6] 看菜自检（Phase 3）：VL 看帧 -> 菜品列表 -> MealMonitor -> 规则引擎。
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

    // 后端：图片 + 提问 -> 回复文本（MakeAidgenVisionBackend，同 [5b] 的图文请求）。
    // 计数供节流/缓存自证：模型调用次数必须与 detect_domain 次数解耦。
    // last_reply 供解析失败时回看模型原话 —— 检测器只交出解析结果，证据留在装配层。
    int model_calls = 0;
    std::string last_reply;
    prosophor::nr::app::VisionQueryBackend backend =
        MakeAidgenVisionBackend(vl, &model_calls, &last_reply);

    // 闭集候选：营养库的 66 个菜名（开放命名是小 VL 模型的弱项；闭集把任务降维
    // 成"识别+从候选里选"）。营养库缺失则退化为开放模式，行为与接库前一致。
    std::vector<std::string> candidates;
    std::string nutrition_error;
    if (const auto table = prosophor::nr::app::DishNutritionTable::LoadFromFile(
            ResolveNutritionFile(prosophor::ProsophorConfig::GetInstance().nutribot),
            nutrition_error);
        table.has_value()) {
        for (const auto& dish : table->dishes()) candidates.push_back(dish.name);
    } else {
        LOG_INFO("  （营养库不可用，退化为开放命名: {}）", nutrition_error.c_str());
    }

    // 产品口径已切到闭集（8B 基准：闭集严格 Top-1 22/72，开放命名 2/72；
    // 3B 时代"闭集退化成抄列表"的结论不再适用）。本节保留两种模式对照。
    // min_interval=0：本节要验的是"同帧缓存"，不是节流窗口（真实帧流用默认 3s）
    prosophor::nr::app::VlFoodDetector detector(backend, /*min_interval_seconds=*/0.0);
    prosophor::nr::app::ImageFrame frame{image};
    LOG_INFO("  闭集候选 {} 道（产品口径=闭集；开放命名保留为对照）", candidates.size());

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
    // 每张图独立走一次 detect_domain，答案与真值当场比对 —— Top-1 严格同名 /
    // Top-1 放宽（包含匹配）/ Top-5 命中（书里承诺指标的口径）三个数字直接打出来；
    // 开放与闭集两种模式各跑一遍，同场 A/B（换模型/换分辨率后重跑即可）。
    {
        const std::string dir = "tests/nutribot/assets/dishes";
        std::set<std::string> files;
        for (const auto& f : prosophor::ListDir(dir, ".jpg")) {
            if (f.find("copy_of") != std::string::npos) continue;   // 调试残留
            files.insert(f);
        }
        const auto labels = LoadDishLabels();
        if (!files.empty()) {
            // 评测跑法：mode_name + 检测器 -> 打印 Top-1/Top-5
            const auto bench = [&](const char* mode_name,
                                   prosophor::nr::app::VlFoodDetector& bench_detector) {
                int blank = 0, total_truth = 0, top1_strict = 0, top1_lenient = 0,
                    top5_hit = 0;
                LOG_INFO("  -- 逐菜批量识别（{} 张，单菜图；{}）--", files.size(), mode_name);
                for (const auto& f : files) {
                    const prosophor::nr::app::ImageFrame dish_frame{
                        prosophor::JoinPath(dir, f)};
                    const auto dish_foods = bench_detector.detect_domain(&dish_frame);
                    if (dish_foods.empty()) ++blank;
                    LOG_INFO("  {}  {}", f.c_str(),
                             dish_foods.empty() ? "（无）" : JoinFoodNames(dish_foods).c_str());

                    const auto truth_it = labels.find(f);
                    if (truth_it == labels.end()) continue;   // 无人工真值：不进统计
                    ++total_truth;
                    const std::string& truth = truth_it->second;
                    if (!dish_foods.empty()) {
                        if (dish_foods[0].name == truth) ++top1_strict;
                        if (SameDish(dish_foods[0].name, truth)) ++top1_lenient;
                        for (size_t i = 0; i < dish_foods.size() && i < 5; ++i) {
                            if (SameDish(dish_foods[i].name, truth)) { ++top5_hit; break; }
                        }
                    }
                }
                LOG_INFO("  [基准] {}：Top-1 严格 {}/{}，Top-1 放宽 {}/{}，Top-5 命中 {}/{}"
                         "（空返回 {} / {} 张）",
                         mode_name, top1_strict, total_truth, top1_lenient, total_truth,
                         top5_hit, total_truth, blank, files.size());
            };

            bench("开放命名（对照）", detector);
            if (!candidates.empty()) {
                prosophor::nr::app::VlFoodDetector closed_detector(
                    backend, /*min_interval_seconds=*/0.0, candidates);
                bench("闭集候选（产品口径）", closed_detector);
            }
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

int NutriBotApp::RunSelfTest() {
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
    PreloadAidliteBeforeAidgen();
    DemoEngineAgent();
    LOG_INFO("[5b] VL 图文自检（aidgen-vl 装配下, 同一 Context 实例）:");
    DemoVlImageCheck();
    LOG_INFO("[5c] 多轮随时给图自检（agent engine，图随消息走，含中途换图）:");
    DemoMultiTurnVision();
    LOG_INFO("[6] 看菜自检（VL 看帧 -> 菜品 -> MealMonitor -> 规则引擎）:");
    DemoFoodRecognition(members);
    LOG_INFO("[7] 营养库自检（菜名 -> 每份营养；识别基准覆盖率）:");
    DemoNutrition();
    LOG_INFO("[8] 机器人控制自检（robot_* 工具 -> HTTP -> 主控遥测）:");
    DemoRobotControl();
    return 0;
}

// ============================================================================
// 常驻回路（Phase 1）：
//   语音：麦克风 → VAD → ASR → AgentEngine → TTS
//   感知：摄像头抓帧 → VL 看菜 → 规则比对 → 主动播报（②③ 段）
//
// 线程划分（设计详见 voice_session.h / audio_io.h / camera_source.h 头注释）：
//   主循环（本函数，100ms tick）—— 唯一 aidgen 调用者（Context 不可并发的
//                                   硬约束）：话轮派发 + 感知节拍 + 饭点 + 退出收尾
//   采集线程 —— ReadFrame → FeedFrame（永不做推理）
//   抓帧线程 —— CameraSource 内部：ffmpeg 单帧抓拍落盘（不碰 aidgen/aidvoice）
//   VoiceSession 内 ASR/TTS 两个 worker —— 只调 aidvoice（与 aidgen 分属
//                                   不同 SDK，互不阻塞）
//
// 主 tick 四段：④ 场景节拍（帧差，每帧，不受播报门控）→ ① 话轮（最高优先）
// → ③ 饭点/小结（便宜，先于感知）→ ② 感知节拍（VL 秒级推理：未在播报、有
// 新帧、过节拍才做，节拍随场景状态收放；期间用户话句落后一轮再答，v1 接受）
// ============================================================================

namespace {

// 退出标志：信号处理器只置原子（信号上下文安全的唯一动作）
std::atomic<bool> g_stop{false};

// config.voice → VoiceSession 调参（帧长固定 480 样本 = 30ms @16k）
prosophor::nr::app::VoiceTuning TuningFromConfig(const prosophor::VoiceConfig& cfg) {
    constexpr int frame_ms = 30;
    prosophor::nr::app::VoiceTuning tuning;
    tuning.vad_rms_dbfs = cfg.vad_rms_dbfs;
    tuning.start_confirm_frames = cfg.start_confirm_frames;
    tuning.end_silence_frames =
        cfg.end_silence_ms / frame_ms > 1 ? cfg.end_silence_ms / frame_ms : 1;
    tuning.max_utterance_frames =
        cfg.max_utterance_ms / frame_ms > 1 ? cfg.max_utterance_ms / frame_ms : 1;
    tuning.pre_roll_frames = cfg.pre_roll_frames;
    return tuning;
}

// 本地时间当日分钟数（饭点判定；localtime_r 线程安全）
int LocalMinutesOfDay() {
    const std::time_t wallclock = std::time(nullptr);
    std::tm local{};
    localtime_r(&wallclock, &local);
    return local.tm_hour * 60 + local.tm_min;
}

// 成员档案目录：配置为空时用 <BaseDir>/nutribot_members。常驻形态持久化 ——
// 不像 selftest 每跑一次清空重建，删目录即恢复出厂（重新预置演示成员）
std::string ResolveMemberStoreDir(const prosophor::NutribotConfig& cfg) {
    if (!cfg.member_store_dir.empty()) {
        return prosophor::ExpandHome(cfg.member_store_dir);
    }
    return prosophor::JoinPath(prosophor::ProsophorConfig::BaseDir().string(),
                               "nutribot_members");
}

// 空档案库预置演示成员：人脸未接，禁忌链路需要"档案库里真有这个人"
// （nutribot.stub_member_name 指向其中之一）。建档走与 selftest [3] 相同的
// 脚本化问答路径 —— 预置档案与交互建档同构，不是另一套旁路。
// 直呼名候选：本餐在场的已识别成员里挑一个该直呼的名字（儿童优先，
// 见 diner_context.h 的 DirectAddress）。名单为空时退回档案库全体的
// 演示档（人脸未接时的"全家可能在"场景），保证饭点问候/小结仍有称呼。
std::string PickSalutation(const std::set<std::string>& active_diner_names,
                           const std::vector<prosophor::nr::domain::Member>& all_members) {
    std::vector<prosophor::nr::domain::Member> active;
    for (const auto& member : all_members) {
        if (active_diner_names.count(member.name) > 0) active.push_back(member);
    }
    if (active.empty()) active = all_members;
    return prosophor::nr::app::DirectAddress(active);
}

void SeedDemoMembersIfEmpty(prosophor::nr::infra::JsonMemberRepository& repo) {
    if (!repo.list_members().empty()) return;
    LOG_INFO("nutribot: 档案库为空 —— 预置演示成员（人脸识别接入后可删库重建）");
    const std::vector<RegistrationCase> cases = {
        {"小明", {"小明", "6", "男", "牛奶、花生", "虾", "少盐", "清淡、甜食",
                  "儿童、乳糖不耐受"}},
        {"王奶奶", {"王奶奶", "68", "女", "蛋糕、肥肉", "没有", "低糖、少盐",
                    "软烂、清淡", "2型糖尿病、老年人"}},
    };
    for (const auto& c : cases) {
        ScriptedDialoguePort dialogue(c.answers);
        prosophor::nr::app::RunQuizCli(repo, std::nullopt, dialogue);
    }
}

}  // namespace

int NutriBotApp::Run(int argc, char** argv) {
    for (int i = 1; argv != nullptr && i < argc; ++i) {
        if (std::string(argv[i]) == "--selftest") {
            return RunSelfTest();
        }
    }
    return RunResident();
}

int NutriBotApp::RunResident() {
    auto& config = prosophor::ProsophorConfig::GetInstance();
    const prosophor::VoiceConfig& voice_cfg = config.voice;

    g_stop.store(false);
    prosophor::Platform::SetConsoleSignalHandler([] { g_stop.store(true); });

    // ── 音频 I/O：采集必须成功（没麦克风就不是语音回路）；播放首次播报时再开 ──
    prosophor::nr::infra::AudioCapture capture;
    prosophor::nr::infra::AudioPlayback playback;
    {
        prosophor::nr::infra::CaptureConfig capture_cfg;
        capture_cfg.device = voice_cfg.capture_device;
        std::string error;
        if (!capture.Open(capture_cfg, error)) {
            LOG_ERROR("nutribot: {}", error.c_str());
            return 1;
        }
    }

    // ── AgentEngine 先构造：其构造里完成 TTS 路由器 Initialize（读配置） ──
    auto& engine = prosophor::AgentEngine::GetInstance();
    prosophor::AsrProviderRouter::GetInstance().Initialize();

    auto* asr_raw = prosophor::AsrProviderRouter::GetInstance().GetRawProvider();
    if (auto* aidvoice_asr = dynamic_cast<prosophor::AidVoiceAsrProvider*>(asr_raw)) {
        std::string error;
        if (!aidvoice_asr->WarmUp(error)) {   // 预创建（约 1.6s），挪出首句
            LOG_ERROR("nutribot: ASR 预热失败: {}", error.c_str());
        }
    }
    auto tts = prosophor::TtsProviderRouter::GetInstance().GetProvider();
    if (auto* aidvoice_tts = dynamic_cast<prosophor::AidVoiceTtsProvider*>(tts.get())) {
        std::string error;
        if (!aidvoice_tts->WarmUp(error)) {   // 预创建（约 2.4s），挪出首播
            LOG_ERROR("nutribot: TTS 预热失败: {}", error.c_str());
        }
    }
    if (asr_raw == nullptr || tts == nullptr) {
        LOG_ERROR("nutribot: 语音后端不可用（asr={} tts={}）—— 检查 settings.json 的 "
                  "asr/tts 段（backend=aidvoice 与模型路径）",
                  asr_raw == nullptr ? "null" : "ok", tts == nullptr ? "null" : "ok");
        return 1;
    }

    // ── 语音会话装配：ASR/TTS 以注入方式进 voice_session（可单测的边界） ──
    prosophor::nr::app::TranscribeFn transcribe =
        [asr_raw](const std::vector<int16_t>& pcm) { return asr_raw->AsrProcess(pcm); };

    // speak 回调里要重置 VAD（播报前后清句），但会话对象在回调之后才构造 ——
    // 用指针占位，worker 首次触发时必然已赋值
    prosophor::nr::app::VoiceSession* voice_ptr = nullptr;
    prosophor::nr::app::SpeakFn speak = [&](const std::string& text) -> bool {
        // 播报期闭麦 + 前后重置 VAD：防自听（v1 不做 barge-in）
        capture.Mute(true);
        prosophor::TtsRequest request;
        request.text = text;
        const prosophor::TtsResponse response = tts->Synthesize(request);
        bool ok = false;
        if (response.success) {
            if (!playback.IsOpen()) {
                prosophor::nr::infra::PlaybackConfig playback_cfg;
                playback_cfg.device = voice_cfg.playback_device;
                playback_cfg.sample_rate = response.sample_rate;
                playback_cfg.channels = response.channels;
                std::string error;
                if (!playback.Open(playback_cfg, error)) {
                    LOG_ERROR("nutribot: {}", error.c_str());
                }
            }
            if (playback.IsOpen()) {
                ok = playback.Write(response.pcm.data(), response.pcm.size()) &&
                     playback.Drain();
            }
        } else {
            LOG_ERROR("nutribot: TTS 合成失败: {}", response.error_msg.c_str());
        }
        capture.Mute(false);
        if (voice_ptr != nullptr) {
            voice_ptr->ResetUtterance();
        }
        return ok;
    };

    prosophor::nr::app::VoiceSession voice(transcribe, speak,
                                           TuningFromConfig(voice_cfg), /*start_workers=*/true);
    voice_ptr = &voice;

    // ── 感知装配（②③ 段）：摄像头 → 桩人脸 → VL 看菜 → 规则 → 播报规划 ──
    // 摄像头/VL 任缺一件只降级感知（语音问答照常），不阻断常驻回路
    prosophor::nr::infra::CameraSource camera;
    bool camera_ok = false;
    {
        prosophor::nr::infra::CameraConfig camera_cfg;
        camera_cfg.device      = config.camera.device;
        camera_cfg.output_dir  = config.camera.output_dir;
        camera_cfg.interval_ms = config.camera.interval_ms;
        camera_cfg.timeout_ms  = config.camera.timeout_ms;
        camera_cfg.keep_frames = config.camera.keep_frames;
        std::string camera_error;
        camera_ok = camera.Start(camera_cfg, camera_error);
        if (!camera_ok) {
            LOG_ERROR("nutribot: 摄像头不可用，主动提醒停用 —— {}", camera_error.c_str());
        }
    }

    // 成员档案库（常驻持久目录；空库预置演示成员）
    const std::string members_dir = ResolveMemberStoreDir(config.nutribot);
    prosophor::EnsureDirectory(members_dir);
    prosophor::nr::infra::JsonMemberRepository members(members_dir);
    SeedDemoMembersIfEmpty(members);

    // 桩人脸：人脸识别未接，固定一位档案成员"在场"（vision_stub.h）
    prosophor::nr::app::StubFaceRecognitionPort stub_vision(config.nutribot.stub_member_name);
    if (!members.get_member(stub_vision.member_name()).has_value()) {
        LOG_ERROR("nutribot: 桩成员「{}」不在档案库 {}，禁忌提醒不会触发"
                  "（改 settings.json 的 nutribot.stub_member_name 为库里已有成员）",
                  stub_vision.member_name().c_str(), members_dir.c_str());
    }

    // 看菜检测器与用餐监控器：懒装配。aidgen 的就绪是**异步三段式**（provider
    // 对象 → 核心/视觉编码器装载 → vlm 配置解析，实测启动后约 4s），启动时
    // 一次性判定必然误判（实测误报"未装配"并把感知路锁死）。主 tick 里按
    // 探针节奏现查，就绪即构造；就绪前感知整段跳过。
    std::unique_ptr<prosophor::nr::app::VlFoodDetector> detector;
    // 营养富化装饰器（包住 VL 检测器；知识库不可用时不包 = 行为与今天一致）
    std::unique_ptr<prosophor::nr::app::NutritionEnrichingDetector> enriched_detector;
    std::unique_ptr<prosophor::nr::app::MealMonitor> monitor;
    // 场景 FSM（帧差运动；按场景节拍驱动，与 VL 懒装配无关）——"自动起止餐"核心。
    // 阈值按 scene_interval_ms 折算：帧更勤（抓帧 1.5s）但判定节拍固定 3s，
    // 时间常数在两个节拍下都准（VL 阻塞主循环时判定节拍自然拉长到 3~4s，同量级）
    std::unique_ptr<prosophor::nr::infra::SceneMonitor> scene_monitor;
    const auto scene_period = std::chrono::milliseconds(
        config.camera.scene_interval_ms > 0 ? config.camera.scene_interval_ms : 3000);
    if (camera_ok) {
        prosophor::nr::infra::MotionConfig motion_cfg;
        motion_cfg.pixel_threshold = config.camera.motion_pixel_threshold;
        scene_monitor = std::make_unique<prosophor::nr::infra::SceneMonitor>(
            prosophor::nr::domain::MakeHoldThresholds(
                static_cast<int>(scene_period.count())),
            motion_cfg);
    }
    // 就餐会话账本（用餐中→餐后 之间的累计，餐后小结用）
    prosophor::nr::app::MealJournal journal;
    prosophor::nr::app::ProactiveAnnouncer announcer({voice_cfg.speak_min_gap_ms});

    // 营养知识库（菜品 -> 每份营养）：加载失败只降级（播报不含热量），不阻断
    std::optional<prosophor::nr::app::DishNutritionTable> nutrition_table;
    {
        const std::string nutrition_path = ResolveNutritionFile(config.nutribot);
        std::string nutrition_error;
        auto loaded =
            prosophor::nr::app::DishNutritionTable::LoadFromFile(nutrition_path, nutrition_error);
        if (loaded.has_value()) {
            if (loaded->skipped_entries() > 0) {
                LOG_WARN("nutribot: 营养库跳过坏条目 {} 条（{}）",
                         loaded->skipped_entries(), nutrition_path.c_str());
            }
            LOG_INFO("nutribot: 营养库已加载 {} 道菜（{}）", loaded->size(),
                     nutrition_path.c_str());
            nutrition_table = std::move(loaded);
        } else {
            LOG_WARN("nutribot: 营养库不可用（{}）—— 播报不含热量", nutrition_error.c_str());
        }
    }

    // 饭点表（非法窗口跳过并告警 —— 配置错不该拦住常驻回路）
    prosophor::nr::app::MealSchedule schedule;
    for (const auto& window : config.meal.windows) {
        if (!schedule.AddWindow(window.name, window.start, window.end)) {
            LOG_WARN("nutribot: 饭点窗口非法已跳过：{} {}-{}", window.name.c_str(),
                     window.start.c_str(), window.end.c_str());
        }
    }
    LOG_INFO("nutribot: 感知路 {}（摄像头 {} / 场景 FSM {} / 桩成员「{}」/ 饭点 {} 段 / 营养库 {} 道）",
             camera_ok ? "待命，VL 就绪后自动开看" : "停用",
             camera_ok ? "ok" : "失败",
             scene_monitor != nullptr ? "已接" : "未接",
             stub_vision.member_name().c_str(), schedule.windows().size(),
             nutrition_table.has_value() ? nutrition_table->size() : 0);

    const std::string session_id = engine.CreateSession("nutribot", "常驻语音会话");
    LOG_INFO("nutribot 常驻回路已启动：采集={} 播放={} 会话={}",
             voice_cfg.capture_device.c_str(), voice_cfg.playback_device.c_str(),
             session_id.c_str());
    LOG_INFO("  >>> 对着麦克风说一句话，或把一道菜放到摄像头前试试（Ctrl-C 退出）");
    ClearLatchedMotion("启动");

    // 工具执行跟踪：动作命令的"没执行就重问"兜底用。回调与主循环同线程，
    // 单个 bool 足够（不加原子）。
    bool turn_had_tool = false;
    engine.AddOutputCallback([&turn_had_tool](
                                  const std::string&, const std::string&,
                                  prosophor::AgentRuntimeState state,
                                  const std::string&,
                                  const std::optional<prosophor::MessageSchema>&,
                                  const std::string&) {
        if (state == prosophor::AgentRuntimeState::EXECUTING_TOOL ||
            state == prosophor::AgentRuntimeState::TOOL_USE) {
            turn_had_tool = true;
        }
    });

    // ── 采集线程：一次排空可用数据 → 按 30ms 切帧（带跨读余数）→ 喂 VAD ──
    std::thread capture_thread([&] {
        constexpr size_t frame_samples = 480;    // 30ms @16k，与 VAD 帧长一致
        std::vector<int16_t> buffer(4800);       // 一次最多取 300ms（抗调度抖动）
        std::vector<int16_t> pending;            // 不足一帧的余数，跨读续接
        int discarded_frames = 0;
        while (!g_stop.load()) {
            const int got = capture.ReadFrame(buffer.data(),
                                              static_cast<int>(buffer.size()), 100);
            if (got <= 0) {
                continue;
            }
            pending.insert(pending.end(), buffer.begin(), buffer.begin() + got);
            size_t offset = 0;
            while (offset + frame_samples <= pending.size()) {
                if (discarded_frames < voice_cfg.warmup_discard_frames) {
                    ++discarded_frames;   // 设备唤醒期可能是数字静音（audio_io.h 已知坑）
                } else {
                    voice.FeedFrame(pending.data() + offset, frame_samples);
                }
                offset += frame_samples;
            }
            pending.erase(pending.begin(),
                          pending.begin() + static_cast<std::ptrdiff_t>(offset));
        }
    });

    // ── 主 tick 四段（唯一 aidgen 调用点）──
    auto next_tick = std::chrono::steady_clock::now();
    const auto perception_period = std::chrono::milliseconds(
        config.camera.perception_interval_ms > 0 ? config.camera.perception_interval_ms
                                                 : 3000);
    const auto idle_perception_period = std::chrono::milliseconds(
        config.camera.idle_interval_ms > 0 ? config.camera.idle_interval_ms : 15000);
    auto next_perception_at = std::chrono::steady_clock::now();
    // 菜品连续命中计数：VL 对非餐桌画面会编菜名（且每帧不同），单帧结果直接
    // 播报会造成"明明没菜也报菜"。同一道菜连续两帧出现才采信。
    std::map<std::string, int> food_streak;
    auto next_scene_at = std::chrono::steady_clock::now();
    auto next_meal_retry_at = std::chrono::steady_clock::now();
    auto next_vl_probe_at = std::chrono::steady_clock::now();
    const auto run_started_at = std::chrono::steady_clock::now();
    bool vl_probe_warned = false;
    std::string last_frame_path;                       // 去重：同一帧只感知一次
    std::string last_scene_frame_path;                 // 场景节拍去重（④ 段）
    std::string current_scene_label;                   // 当前场景状态（FSM 标签）
    bool faces_latest = false;                         // 最近一轮 VL 的"有人"信号
    bool foods_latest = true;                          // 最近一轮 VL 的"有菜"信号（未知按有）
    std::string current_meal_name;                     // 上一 tick 所处饭点（空 = 不在窗口）
    std::optional<std::string> pending_meal_greeting;  // 节流中挂起的饭点问候
    std::optional<prosophor::nr::app::MealSummary> pending_meal_summary;  // 挂起的餐后小结
    std::set<std::string> active_diner_names;          // 本餐已识别的成员名（开餐清空）
    bool visitor_mode = false;                         // 访客静默：本帧无注册成员在场
    int silence_skipped = 0;                           // 因访客静默放弃的主动播报次数（诊断）
    int tick_count = 0;

    while (!g_stop.load()) {
        const auto now = std::chrono::steady_clock::now();

        // 感知件懒装配：每 10s 探一次 VL 就绪（三段式异步），就绪才构造检测器/
        // 监控器；30s 仍不就来一句告警（装载失败/未装配），之后不再刷屏
        if (camera_ok && monitor == nullptr && now >= next_vl_probe_at) {
            next_vl_probe_at = now + std::chrono::seconds(10);
            auto provider =
                prosophor::LlmProviderRouter::GetInstance().GetProviderByName("aidgen");
            auto* vl_candidate = dynamic_cast<prosophor::AidGenProvider*>(provider.get());
            if (vl_candidate != nullptr && vl_candidate->IsLoaded() &&
                vl_candidate->HasVisionConfig()) {
                // 闭集模式（知识库口径）：候选来自营养库菜名 —— assets/labels.tsv
                // 的 59 个"知识库种子"菜名被营养库 66 道全覆盖。8B 基准：闭集
                // 严格 Top-1 22/72，开放命名只有 2/72（3B 时代"闭集更差"的结论
                // 不再适用）；非餐桌画面的闭集误报由连续两帧稳定性门控兜底。
                std::vector<std::string> candidates;
                if (nutrition_table.has_value()) {
                    for (const auto& dish : nutrition_table->dishes()) {
                        candidates.push_back(dish.name);
                    }
                }
                detector = std::make_unique<prosophor::nr::app::VlFoodDetector>(
                    MakeAidgenVisionBackend(vl_candidate, nullptr, nullptr),
                    /*min_interval_seconds=*/3.0, std::move(candidates));
                // 营养富化装饰器：知识库可用才包（包住 VL 检测器后，规则引擎与
                // 账本拿到的菜就带每份营养；知识库缺失则行为与接库前完全一致）
                prosophor::nr::app::FoodDetectorPort* detector_port = detector.get();
                if (nutrition_table.has_value()) {
                    enriched_detector = std::make_unique<
                        prosophor::nr::app::NutritionEnrichingDetector>(
                        detector.get(), &*nutrition_table);
                    detector_port = enriched_detector.get();
                }
                monitor = std::make_unique<prosophor::nr::app::MealMonitor>(
                    &stub_vision, &members, nullptr, scene_monitor.get(), detector_port);
                LOG_INFO("nutribot: VL 就绪，感知路启用（后台装载耗时 {:.1f}s）",
                         std::chrono::duration<double>(now - run_started_at).count());
            } else if (!vl_probe_warned && now - run_started_at > std::chrono::seconds(30)) {
                vl_probe_warned = true;
                LOG_WARN("nutribot: VL 30s 未就绪（aidgen {}），看菜保持停用：{}",
                         vl_candidate == nullptr
                             ? "未装配"
                             : (vl_candidate->IsLoading() ? "装载中" : "装载失败"),
                         vl_candidate == nullptr ? "" : vl_candidate->GetLoadError().c_str());
            }
        }

        // ④ 场景节拍：按 scene_period 取最新帧判定一次（纯 opencv ~30ms，不耗
        // NPU）—— 刻意不受 !speaking 门控：播报期场景照常推进，否则"安静 90s"
        // 这类时间常数会被语音活动悄悄拉长；也要比 VL 快，起餐探测才不被慢节拍吃掉
        if (scene_monitor != nullptr && now >= next_scene_at) {
            const std::string frame_path = camera.LatestFrame();
            if (!frame_path.empty() && frame_path != last_scene_frame_path) {
                last_scene_frame_path = frame_path;
                next_scene_at = now + scene_period;   // 判定节拍固定（抓帧更勤只赚新鲜度）
                const prosophor::nr::app::ImageFrame frame{frame_path};
                const std::string previous_label = current_scene_label;
                current_scene_label =
                    scene_monitor->update(&frame, faces_latest, foods_latest);

                if (current_scene_label != previous_label) {
                    LOG_INFO("[场景] {} → {}（运动 {:.3f} / 人脸 {} / 菜 {}，累计转移 {}）",
                             previous_label.empty() ? "（启动）" : previous_label.c_str(),
                             current_scene_label.c_str(), scene_monitor->last_motion_ratio(),
                             faces_latest ? "有" : "无", foods_latest ? "有" : "无",
                             scene_monitor->transitions());
                    if (current_scene_label == "用餐中") {
                        journal.BeginMeal(now);   // 开饭：开始记这顿的账
                        active_diner_names.clear();   // 新的一餐：在场成员重新认定
                    } else if (current_scene_label == "餐后") {
                        if (auto summary = journal.EndMeal(now);
                            summary.has_value() && config.meal.announce) {
                            pending_meal_summary = std::move(summary);   // 小节话术稍后播
                        }
                    }
                    // 状态变了立刻看一眼桌面：把"起餐时菜还没被 VL 认出来"的
                    // 虚惊回落窗口压到一轮 VL 之内（见 scene.cc 的窗口注记）
                    next_perception_at = now;
                }
                // 有动静也把下一轮看菜拉到眼前：空闲态干等 15s 的节拍是"放菜到
                // 播报"时延的大头（实测 22s 里占 15s）—— 动静过去后自然退回慢节拍
                if (scene_monitor->last_motion_ratio() >
                    prosophor::nr::domain::SceneStateMachine::MOTION_LOW) {
                    if (next_perception_at > now) next_perception_at = now;
                }
            }
        }

        // ① 话轮：用户说话 → 会话回复 → 播报（最高优先）。响应路不受访客
        // 静默约束 —— 被点名要能回应，静默只针对"主动开口"
        if (!voice.speaking()) {
            if (auto text = voice.PollUtterance(0)) {
                if (!IsMeaningfulUtterance(*text)) {
                    LOG_INFO("[忽略] 无有效中文（{}）", text->c_str());
                    continue;
                }
                if (!IsDirectedUtterance(*text)) {
                    LOG_INFO("[忽略] 非定向话轮（{}）", text->c_str());
                    continue;
                }
                LOG_INFO("[用户] {}", text->c_str());
                // 语气自适应：在场成员档案压成提示块随消息注入（system 保持
                // 稳定，KV 前缀不受影响；块内容见角色 prompt 的成员约定）
                std::vector<prosophor::nr::domain::Member> diners;
                if (!active_diner_names.empty()) {
                    for (const auto& member : members.list_members()) {
                        if (active_diner_names.count(member.name) > 0) {
                            diners.push_back(member);
                        }
                    }
                }
                const std::string prompt_block =
                    prosophor::nr::app::ComposeDinerPromptBlock(diners);
                // 动作命令贴短标记：紧邻生成点，抵消"照抄上一轮已完成回复"的惯性
                const std::string action_prefix =
                    IsActionCommand(*text) ? "【动作命令】" : "";
                const std::string user_message = prompt_block + action_prefix + *text;
                turn_had_tool = false;
                std::string reply = engine.SendUserMessage(session_id, user_message);
                // 动作未执行兜底：小模型会照抄历史里"已完成"的文字回复而不调用工具
                //（实测连说两遍"做个开心的表情"，第二遍只回"做得开心"）。动作命令
                // 但本轮无工具执行 → 重问一次并强调必须调用。
                if (IsActionCommand(*text) && !turn_had_tool) {
                    LOG_INFO("[动作未执行] 重问一次（原话: {}）", reply.c_str());
                    turn_had_tool = false;
                    reply = engine.SendUserMessage(
                        session_id,
                        prompt_block +
                            "（旁白：这是动作命令，必须调用工具执行，不要用一句话代替）\n" +
                            action_prefix + *text);
                }
                // 沉默兜底：0.5B 对"可以不接话"的出口过拟合（连"有什么推荐"都
                // 沉默），规则认为该答就重问一次 —— 话术质量次要，把话接住优先
                if (!reply.empty() && prosophor::nr::app::IsSilentReply(reply) &&
                    prosophor::nr::app::ShouldAnswerUtterance(*text)) {
                    LOG_INFO("[沉默] 但句子需要回应，重问一次（原话: {}）", reply.c_str());
                    reply = engine.SendUserMessage(
                        session_id,
                        prompt_block +
                            "（旁白：刚才你判断保持沉默，但这句话是直接问你的，请作出简短回答）\n" +
                            action_prefix + *text);
                }
                // 空回复兜底：0.5B 偶发一上来就吐 EOS（零内容），与沉默过拟合同源，
                // 重问一次；问句/动作命令被吞同样是"把话接住"优先
                if (reply.empty()) {
                    LOG_INFO("[空回复] 重问一次");
                    reply = engine.SendUserMessage(
                        session_id,
                        prompt_block +
                            "（旁白：刚才你没有输出任何内容，请直接回应这句话）\n" +
                            action_prefix + *text);
                }
                // 小模型会把重问旁白当正文复述（实测："刚才你没有输出任何内容，
                // 请直接回应这句话。"被原样念出来）—— 复述不是回复，按失败处理
                const bool retry_narration_echo =
                    reply.find("刚才你没有输出任何内容") != std::string::npos ||
                    reply.find("刚才你判断保持沉默") != std::string::npos ||
                    reply.find("这是动作命令") != std::string::npos;
                if (reply.empty() || retry_narration_echo) {
                    LOG_WARN("nutribot: 会话无有效回复（{}）",
                             retry_narration_echo ? "复述了重问旁白" : "空");
                } else if (prosophor::nr::app::IsSilentReply(reply)) {
                    // LLM 自主判断不接话（角色 prompt 的沉默约定）：话进了对话
                    // 上下文，但不开口 —— 桌边闲聊不该句句插嘴
                    LOG_INFO("[沉默] 判断无需回应，仅记录上下文（原话: {}）", reply.c_str());
                } else {
                    const std::string speech = prosophor::nr::app::SanitizeForSpeech(
                        prosophor::nr::app::StripSilenceMarker(reply),
                        voice_cfg.speak_max_chars);
                    if (!speech.empty()) {
                        LOG_INFO("[助手] {}", speech.c_str());
                        voice.Speak(speech);
                        announcer.NoteConversationSpeech(now);   // 主动播报不追着对话抢麦
                    }
                }
            }
        }

        // ③ 饭点：进入窗口问候一次（节流中挂起，按感知节拍重试）；排在感知前 ——
        // 问候是瞬时的，不该排在一次秒级 VL 推理后面。问候承诺"我帮你看看"，
        // 故与感知同步启用（VL 未就绪时不问候）
        if (monitor != nullptr && !voice.speaking()) {
            const std::vector<prosophor::nr::domain::Member> all_members =
                members.list_members();   // 本 tick 快照：打招呼查询用
            const auto window = schedule.Lookup(LocalMinutesOfDay());
            const std::string meal_now = window.has_value() ? window->name : std::string();
            if (meal_now != current_meal_name) {
                current_meal_name = meal_now;
                if (!meal_now.empty()) {
                    LOG_INFO("[饭点] 进入「{}」窗口", meal_now.c_str());
                    pending_meal_greeting = meal_now;
                }
            }
            // 访客静默（书 1.2/3.3）：本帧未识别到注册成员（客人或无人）时，
            // 主动播报整体转静默 —— 问候挂在 pending 上等下轮（只是推迟，不丢）；
            // 小结没有重播时机，放弃并计数（下次"餐后"会有新账）
            // 饭点问候（受 meal.announce 管：报菜名/禁忌提醒不受它管，那是安全/信息类）
            if (pending_meal_greeting.has_value() && config.meal.announce &&
                now >= next_meal_retry_at) {
                next_meal_retry_at = now + perception_period;
                if (visitor_mode) {
                    ++silence_skipped;   // 访客在座：问候等下轮
                } else {
                    const std::string speech = announcer.PlanMealEntry(
                        *pending_meal_greeting, now,
                        PickSalutation(active_diner_names, all_members));
                    if (!speech.empty()) {
                        LOG_INFO("[主动] {}", speech.c_str());
                        voice.Speak(speech);
                        pending_meal_greeting.reset();
                    }
                }
            }
            // 餐后小结（进入"餐后"时记账产出；节流中挂起，按感知节拍重试）
            if (pending_meal_summary.has_value() && now >= next_meal_retry_at) {
                next_meal_retry_at = now + perception_period;
                if (visitor_mode) {
                    ++silence_skipped;
                    pending_meal_summary.reset();
                } else {
                    const std::string speech = announcer.PlanMealSummary(
                        *pending_meal_summary, now,
                        PickSalutation(active_diner_names, all_members));
                    if (!speech.empty()) {
                        LOG_INFO("[主动] {}", speech.c_str());
                        const std::string sanitized = prosophor::nr::app::SanitizeForSpeech(
                            speech, voice_cfg.speak_max_chars);
                        if (!sanitized.empty()) voice.Speak(sanitized);
                        pending_meal_summary.reset();
                    }
                }
            }
        }

        // ② 感知节拍：有新帧、未在播报、过节拍 → 看菜 → 规则 → 播报计划。
        // 节拍随场景状态收放：备餐/用餐全速（3s），空闲/餐后放慢（15s）—— VL 是
        // 板上最大的一块 NPU 开销，桌上没饭时不必每 3 秒看一眼
        // 刻意不带 !voice.speaking()：播报期麦克风已闭麦、看菜与 TTS 分属两条
        // 线程，提醒不该因为"机器人正在说话"被推后（播报队列深度 1 丢旧保新）
        if (monitor != nullptr && now >= next_perception_at) {
            const std::string frame_path = camera.LatestFrame();
            if (!frame_path.empty() && frame_path != last_frame_path) {
                last_frame_path = frame_path;
                const bool scene_active =
                    (current_scene_label == "备餐中" || current_scene_label == "用餐中");
                // 节拍从本轮起点起算（VL 耗时不计入）
                next_perception_at =
                    now + (scene_active ? perception_period : idle_perception_period);
                const prosophor::nr::app::ImageFrame frame{frame_path};
                const auto pkg = monitor->Tick(&frame);
                // 幻觉抑制：本帧出现过的菜记连续计数，连续 ≥2 帧才进入场景包
                std::map<std::string, int> next_streak;
                for (const auto& food : pkg.foods) {
                    const auto it = food_streak.find(food.name);
                    next_streak[food.name] = (it == food_streak.end() ? 1 : it->second + 1);
                }
                food_streak = std::move(next_streak);
                auto stable_pkg = pkg;
                stable_pkg.foods.clear();
                for (const auto& food : pkg.foods) {
                    if (food_streak[food.name] >= 2) stable_pkg.foods.push_back(food);
                }
                // 场景 FSM 的输入信号取自最近一轮 VL（未答先按"有人有菜"的旧语义）
                faces_latest = !stable_pkg.members.empty();
                foods_latest = !stable_pkg.foods.empty();
                journal.Observe(stable_pkg);   // 用餐中的账：菜品/热量/提醒累计
                // 本餐在场成员认定（识别到谁记谁；开餐清空、跨轮累积）
                for (const auto& member : pkg.members) {
                    if (!member.name.empty()) active_diner_names.insert(member.name);
                }
                const bool visitor_now = prosophor::nr::app::VisitorSilence(pkg.members);
                if (visitor_now != visitor_mode) {
                    LOG_INFO("[在场] {}（本餐已识别：{}）", visitor_now ? "无注册成员，转静默" : "识别到家庭成员",
                             active_diner_names.empty() ? "无" : JoinNames(active_diner_names).c_str());
                    visitor_mode = visitor_now;
                }
                LOG_DEBUG("nutribot: 看菜 -> 稳定 {} 道（原始 {}）: {}",
                          stable_pkg.foods.size(), pkg.foods.size(),
                          JoinFoodNames(stable_pkg.foods).c_str());
                // 访客静默：识别到谁都不认识的画面（客人/无人）时不主动开口；
                // PlanPerception 照喂 —— 内部登记/去重状态要随感知推进
                const std::string speech =
                    visitor_mode ? std::string() : announcer.PlanPerception(stable_pkg, now);
                if (visitor_mode) ++silence_skipped;
                if (!speech.empty()) {
                    LOG_INFO("[主动] {}", speech.c_str());
                    const std::string sanitized = prosophor::nr::app::SanitizeForSpeech(
                        speech, voice_cfg.speak_max_chars);
                    if (!sanitized.empty()) voice.Speak(sanitized);
                }
            }
        }

        if (++tick_count % 300 == 0) {   // 每 30s 打点一次统计
            const auto stats = voice.stats();
            const auto ann = announcer.stats();
            std::string member_label;
            if (visitor_mode) {
                member_label = "访客静默";
            } else if (active_diner_names.empty()) {
                member_label = "未识别";
            } else {
                member_label = JoinNames(active_diner_names);
            }
            LOG_INFO("[统计] 帧={} 句={} 播报={} 丢弃(短/弱/忙)={}/{}/{} ASR空={} | "
                     "抓帧={} 看菜={} 提醒(播/挂)={}/{} 报菜名={} 饭点={} 小结={} 节流推迟={} | "
                     "场景={} 转移={} 账本={} | 成员={} 静默跳过={}",
                     stats.feed_frames, stats.utterances, stats.spoken,
                     stats.dropped_short, stats.dropped_lowpeak, stats.dropped_busy,
                     stats.asr_empty, camera.frame_count(),
                     detector != nullptr ? detector->query_count() : 0,
                     ann.reminders_spoken, ann.pending_reminders,
                     ann.food_announcements, ann.meal_announcements, ann.meal_summaries,
                     ann.suppressed_by_gap,
                     current_scene_label.empty() ? "未知" : current_scene_label.c_str(),
                     scene_monitor != nullptr ? scene_monitor->transitions() : 0,
                     journal.active() ? "记账中" : "空闲",
                     member_label.c_str(), silence_skipped);
        }
        next_tick += std::chrono::milliseconds(100);
        std::this_thread::sleep_until(next_tick);
    }

    // ── 退出收尾（main 的 std::_Exit 跳过静态析构，顺序敏感）──
    LOG_INFO("nutribot: 退出中……");
    capture.Mute(true);
    if (capture_thread.joinable()) {
        capture_thread.join();
    }
    capture.Close();
    camera.Stop();      // 抓帧线程先停：此后不再起新的 ffmpeg 抓拍
    playback.Abort();   // 让在飞的播放立即返回
    voice.Stop();       // join ASR/TTS worker：此后不再有 SDK 调用
    prosophor::AsrProviderRouter::GetInstance().DestroyProvider();
    prosophor::TtsProviderRouter::GetInstance().ReleaseProviders();
    playback.Close();
    ClearLatchedMotion("退出");
    const auto stats = voice.stats();
    const auto ann = announcer.stats();
    LOG_INFO("nutribot: 统计 帧={} 句={} 播报={} 丢弃(短/弱/忙)={}/{}/{} ASR空={} | "
             "抓帧={} 看菜={} 提醒(播/挂)={}/{} 报菜名={} 饭点={} 小结={} 节流推迟={} | "
             "场景={} 转移={}",
             stats.feed_frames, stats.utterances, stats.spoken, stats.dropped_short,
             stats.dropped_lowpeak, stats.dropped_busy, stats.asr_empty,
             camera.frame_count(), detector != nullptr ? detector->query_count() : 0,
             ann.reminders_spoken, ann.pending_reminders, ann.food_announcements,
             ann.meal_announcements, ann.meal_summaries,
             ann.suppressed_by_gap,
             current_scene_label.empty() ? "未知" : current_scene_label.c_str(),
             scene_monitor != nullptr ? scene_monitor->transitions() : 0);
    LOG_INFO("nutribot 常驻循环结束");
    return 0;
}

}  // namespace prosophor::nr::nutribot
