// MealMonitor 用例测试：Fake 端口注入，脱离硬件全链路
#include <map>
#include <string>
#include <vector>

#include "meal/meal_monitor.h"
#include "ports.h"
#include "micro_test.hpp"

using namespace prosophor::nr::app;
using prosophor::nr::domain::FoodItem;
using prosophor::nr::domain::Member;
using prosophor::nr::domain::MemberRepository;
using prosophor::nr::domain::ReminderEvent;
using prosophor::nr::domain::ScenePackage;
using prosophor::nr::domain::SceneState;

namespace {

// ---- Fake：人脸识别端口 ----
class FakeVision : public FaceRecognitionPort {
public:
    std::vector<FaceObservation> result;
    int calls = 0;
    std::vector<FaceObservation> detect_and_recognize(
        FrameRef, MemberRepository&, double, int) override {
        ++calls;
        return result;
    }
    void release() override {}
};

// ---- Fake：成员仓储（内存 map） ----
class FakeRepo : public MemberRepository {
public:
    std::map<std::string, Member> store;

    std::optional<prosophor::nr::domain::MemberMatch> find_match(
        const std::vector<float>&, double) override {
        return std::nullopt;
    }
    std::vector<Member> list_members() override {
        std::vector<Member> out;
        for (auto& [k, v] : store) out.push_back(v);
        return out;
    }
    std::optional<Member> get_member(const std::string& name) override {
        auto it = store.find(name);
        if (it == store.end()) return std::nullopt;
        return it->second;
    }
    void add_member(const Member& m) override { store[m.name] = m; }
    void delete_member(const std::string& name) override { store.erase(name); }
};

// ---- Fake：场景端口（直接返回设定状态，记录 force） ----
class FakeScene : public SceneMonitorPort {
public:
    std::string current = "空桌待机";
    bool last_has_faces = false;
    bool last_has_foods = true;
    bool forced = false;

    std::string update(FrameRef, bool has_faces, bool has_foods) override {
        last_has_faces = has_faces;
        last_has_foods = has_foods;
        return current;
    }
    void force(std::string_view label) override {
        forced = true;
        current = std::string(label);
    }
    std::string motion_level() const override { return "low"; }
};

// ---- Fake：菜品端口 ----
class FakeFood : public FoodDetectorPort {
public:
    std::vector<FoodItem> foods;
    std::vector<FoodItem> detect_domain(FrameRef) override { return foods; }
};

// ---- Fake：LLM 场景判据 ----
class FakeJudge : public SceneJudgingPort {
public:
    std::optional<SceneJudgement> next;
    int calls = 0;
    std::optional<SceneJudgement> judge(const SceneJudgeObservation&) override {
        ++calls;
        return next;
    }
    void release() override {}
};

Member make_member(const std::string& name,
                   std::vector<std::string> taboos = {}) {
    Member m;
    m.member_id = "id-" + name;
    m.name = name;
    m.profile.taboo_foods = std::move(taboos);
    return m;
}

FaceObservation known_face(const std::string& name, double sim) {
    FaceObservation o;
    o.name = name;
    o.similarity = sim;
    return o;
}

}  // namespace

// 1) 无场景机/无判据：确定轨缺席 -> scene_state "unknown"；规则照常比对
TEST(meal_monitor, no_scene_pure_rules_flow) {
    FakeVision vision;
    vision.result = {known_face("小明", 0.812345), known_face("", 0.4)};
    FakeRepo repo;
    repo.add_member(make_member("小明", {"牛奶"}));
    FakeFood food;
    food.foods = {FoodItem("f1", "牛奶")};

    MealMonitor mm(&vision, &repo, nullptr, nullptr, &food);
    const ScenePackage pkg = mm.Tick(nullptr);

    EXPECT_EQ(pkg.scene_state, "unknown");
    EXPECT_EQ(pkg.members.size(), 2u);
    EXPECT_NEAR(pkg.members[0].similarity, 0.812, 1e-9);   // round3
    EXPECT_EQ(pkg.events.size(), 1u);                      // 只有已知成员触发
    EXPECT_EQ(pkg.events[0].kind, "taboo");
    EXPECT_EQ(pkg.events[0].message, "小明：牛奶上桌了，你的禁忌食物哦");
}

// 2) 判据缺席 + 场景机在场：纯状态机输出（行为与 Python 无 judge 一致）
TEST(meal_monitor, fsm_only_output) {
    FakeVision vision;   // 空人脸
    FakeRepo repo;
    FakeScene scene;
    scene.current = "备餐中";

    MealMonitor mm(&vision, &repo, nullptr, &scene, nullptr, nullptr);
    const ScenePackage pkg = mm.Tick(nullptr);

    EXPECT_EQ(pkg.scene_state, "备餐中");
    EXPECT_EQ(scene.last_has_faces, false);
    EXPECT_EQ(scene.last_has_foods, false);   // 无菜品端口 -> 桌上无菜
    EXPECT_TRUE(pkg.events.empty());
}

// 2b) 场景机在场且有菜：has_foods 透传（止餐信号的下游依赖）
TEST(meal_monitor, scene_receives_has_foods) {
    FakeVision vision;
    FakeRepo repo;
    FakeScene scene;
    scene.current = "用餐中";
    FakeFood food;
    food.foods = {FoodItem("f1", "清炒时蔬")};

    MealMonitor mm(&vision, &repo, nullptr, &scene, &food);
    const ScenePackage pkg = mm.Tick(nullptr);
    EXPECT_EQ(pkg.scene_state, "用餐中");
    EXPECT_EQ(scene.last_has_foods, true);
}

// 3) 复核轨 cadence + 投票窗：LLM 连续 agree_window 次一致才覆盖 FSM
TEST(meal_monitor, judge_cadence_and_voter_override) {
    FakeVision vision;
    vision.result = {known_face("小明", 0.9)};
    FakeRepo repo;
    FakeScene scene;
    scene.current = "备餐中";
    FakeFood food;   // 无菜
    FakeJudge judge;
    judge.next = SceneJudgement{SceneState::DINING, 0.8, "有进食动作"};

    // cadence=2, 窗口=2 -> 第 2、4 个 tick 触发判据
    MealMonitor mm(&vision, &repo, nullptr, &scene, nullptr, &judge,
                   /*judge_every_ticks=*/2, /*agree_window=*/2);

    ScenePackage p1 = mm.Tick(nullptr);
    EXPECT_EQ(p1.scene_state, "备餐中");     // tick1: 未到 cadence
    ScenePackage p2 = mm.Tick(nullptr);      // tick2: 判据投 DINING(1/2)
    EXPECT_EQ(p2.scene_state, "备餐中");
    EXPECT_EQ(judge.calls, 1);
    ScenePackage p3 = mm.Tick(nullptr);
    EXPECT_EQ(p3.scene_state, "备餐中");     // tick3
    ScenePackage p4 = mm.Tick(nullptr);      // tick4: DINING(2/2) -> 覆盖
    EXPECT_EQ(p4.scene_state, "用餐中");
    EXPECT_EQ(judge.calls, 2);
    EXPECT_TRUE(scene.forced);               // FSM 被 force 同步
    EXPECT_EQ(scene.current, "用餐中");
}

// 4) 判据返回与 FSM 一致/无法判定：不覆盖
TEST(meal_monitor, judge_agrees_with_fsm_no_switch) {
    FakeVision vision;
    FakeRepo repo;
    FakeScene scene;
    scene.current = "用餐中";
    FakeJudge judge;
    judge.next = SceneJudgement{SceneState::DINING};   // 与 FSM 一致
    MealMonitor mm(&vision, &repo, nullptr, &scene, nullptr, &judge,
                   /*every=*/1, /*window=*/2);
    const ScenePackage pkg = mm.Tick(nullptr);
    EXPECT_EQ(pkg.scene_state, "用餐中");
    EXPECT_FALSE(scene.forced);
}

// 5) 未知成员不触发规则；无菜无事件
TEST(meal_monitor, unknown_faces_no_events) {
    FakeVision vision;
    vision.result = {known_face("", 0.3)};   // unknown
    FakeRepo repo;
    repo.add_member(make_member("小明", {"牛奶"}));
    FakeFood food;
    food.foods = {FoodItem("f1", "牛奶")};

    MealMonitor mm(&vision, &repo, nullptr, nullptr, &food);
    const ScenePackage pkg = mm.Tick(nullptr);
    EXPECT_TRUE(pkg.events.empty());
}
