// MealMonitor 实现 —— 语义逐行对照 Python application/meal_monitor.py
#include "meal/meal_monitor.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace prosophor::nr::app {

namespace {

double round3(double v) {  // Python round(v, 3)
    return std::round(v * 1000.0) / 1000.0;
}

}  // namespace

MealMonitor::MealMonitor(FaceRecognitionPort* vision,
                         prosophor::nr::domain::MemberRepository* members,
                         prosophor::nr::domain::NutritionRuleEngine* rules,
                         SceneMonitorPort* scene_monitor,
                         FoodDetectorPort* food_detector,
                         SceneJudgingPort* scene_judge,
                         int judge_every_ticks,
                         int agree_window)
    : vision_(vision), members_(members), rules_(rules ? rules : &default_rules_),
      scene_(scene_monitor), food_(food_detector), judge_(scene_judge),
      judge_every_(std::max(1, judge_every_ticks)) {
    if (judge_ != nullptr) {
        voter_ = std::make_unique<prosophor::nr::domain::StateVoter>(agree_window);
    }
}

prosophor::nr::domain::ScenePackage MealMonitor::Tick(FrameRef frame, double threshold,
                                           int min_size) {
    // 1) 感知：人脸检测 + 识别（实现负责帧内节流）
    std::vector<FaceObservation> obs_list =
        vision_->detect_and_recognize(frame, *members_, threshold, min_size);

    // 2) 菜品（Phase 3；检测器未注入则跳过）
    std::vector<prosophor::nr::domain::FoodItem> foods;
    if (food_) foods = food_->detect_domain(frame);

    // 3) 场景状态：确定轨 +（可选）复核轨
    std::string scene_state = "unknown";
    if (scene_) {
        // 复用当帧人脸结果（has_faces）与当帧菜品（has_foods），不重复感知
        const std::string fsm_label = scene_->update(
            frame, /*has_faces=*/!obs_list.empty(), /*has_foods=*/!foods.empty());
        scene_state = judge_ ? JudgeScene(obs_list, foods, fsm_label)
                             : fsm_label;
    }

    // 4) 领域服务：规则比对（禁忌 + 营养素）
    std::vector<const FaceObservation*> known;
    for (const auto& o : obs_list) {
        if (o.known()) known.push_back(&o);
    }
    std::vector<prosophor::nr::domain::ReminderEvent> events;
    if (!known.empty() && !foods.empty()) {
        std::vector<prosophor::nr::domain::Member> domain_members;
        for (const auto* o : known) {
            auto m = members_->get_member(*o->name);
            if (m.has_value()) domain_members.push_back(std::move(*m));
        }
        events = rules_->check_taboos(domain_members, foods);
        const auto nutrition = rules_->check_nutrition(domain_members, foods);
        events.insert(events.end(), nutrition.begin(), nutrition.end());
    }

    // 5) 结构化场景包
    prosophor::nr::domain::ScenePackage pkg;
    pkg.timestamp = 0.0;  // 时间戳由上层（真实时钟）填充；Python 同值为 0.0
    pkg.scene_state = scene_state;
    for (const auto& o : obs_list) {
        prosophor::nr::domain::MemberBrief b;
        b.name = o.name.value_or("");
        b.similarity = round3(o.similarity);
        b.tags = o.health_tags;
        b.taboo = o.taboo_foods;
        b.box = o.box;
        pkg.members.push_back(std::move(b));
    }
    pkg.foods = std::move(foods);
    pkg.events = std::move(events);
    return pkg;
}

std::string MealMonitor::JudgeScene(
    const std::vector<FaceObservation>& obs_list,
    const std::vector<prosophor::nr::domain::FoodItem>& foods,
    const std::string& fsm_label) {
    // FSM 一旦自行转移立即 resync（信任确定轨并清票）
    const auto cur = prosophor::nr::domain::scene_from_label(fsm_label);
    if (cur.has_value() && *cur != prosophor::nr::domain::SceneState::UNKNOWN &&
        cur != voter_->current()) {
        voter_->resync(*cur);
    }
    ++tick_;
    if (tick_ % judge_every_ != 0) {
        return fsm_label;  // 未到 cadence：纯状态机输出
    }

    SceneJudgeObservation payload;
    payload.current_state = fsm_label;
    for (const auto& o : obs_list) {
        if (o.name.has_value()) payload.members.push_back(*o.name);
    }
    std::set<std::string> food_names;  // Python: sorted({f.name})
    for (const auto& f : foods) food_names.insert(f.name);
    payload.foods.assign(food_names.begin(), food_names.end());
    payload.motion_level = scene_->motion_level();

    std::optional<SceneJudgement> judgement;
    try {
        judgement = judge_->judge(payload);
    } catch (...) {  // 判据异常不阻塞主循环
        judgement = std::nullopt;
    }
    const auto candidate =
        judgement.has_value()
            ? std::optional<prosophor::nr::domain::SceneState>(judgement->state)
            : std::nullopt;

    const prosophor::nr::domain::SceneState confirmed = voter_->vote(candidate);
    const std::string confirmed_label(prosophor::nr::domain::scene_label(confirmed));
    if (confirmed_label != fsm_label) {
        scene_->force(confirmed_label);  // 同步 FSM，防旧状态计数器误转
    }
    return confirmed_label;
}

}  // namespace prosophor::nr::app
