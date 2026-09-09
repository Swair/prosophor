// 用例：MealMonitor 用餐监控会话
//
// 移植自 Python application/meal_monitor.py（语义 1:1，可脱离硬件单测）。
//
// 编排（每周期一次 Tick）：
//   感知(人脸识别) -> 场景状态 -> 菜品(Phase 3) -> 规则比对 -> 结构化场景包
//
// 场景状态两轨（Phase 5 骨架，默认仅确定轨，行为与纯状态机一致）：
//   确定轨: SceneMonitorPort（帧差运动 + FSM）—— 默认权威，驱动感知节流
//   复核轨: SceneJudgingPort(LLM) 低频判据 —— 候选经 StateVoter 连续
//           agree_window 次一致才覆盖确定轨；无判据时整轨不参与
//
// 依赖注入：所有依赖经构造器传入（Port / 仓储 / 领域服务），不触硬件。
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ports.h"
#include "events.h"
#include "rule_engine.h"
#include "scene.h"

namespace prosophor::nr::app {

class MealMonitor {
public:
    MealMonitor(FaceRecognitionPort* vision,
                prosophor::nr::domain::MemberRepository* members,
                prosophor::nr::domain::NutritionRuleEngine* rules = nullptr,
                SceneMonitorPort* scene_monitor = nullptr,
                FoodDetectorPort* food_detector = nullptr,
                SceneJudgingPort* scene_judge = nullptr,
                int judge_every_ticks = 150,
                int agree_window = 3);

    // 处理一帧画面并产出结构化场景包（Agent 层消费）
    prosophor::nr::domain::ScenePackage Tick(FrameRef frame, double threshold = 0.5,
                                  int min_size = 40);

    // 已累积 tick 数（供诊断）
    int tick_count() const { return tick_; }

private:
    // 复核轨：LLM 低频判据 + 投票窗确认，覆盖 FSM 初判
    std::string JudgeScene(const std::vector<FaceObservation>& obs_list,
                           const std::vector<prosophor::nr::domain::FoodItem>& foods,
                           const std::string& fsm_label);

    FaceRecognitionPort* vision_;
    prosophor::nr::domain::MemberRepository* members_;
    prosophor::nr::domain::NutritionRuleEngine default_rules_;      // 未注入时自备
    prosophor::nr::domain::NutritionRuleEngine* rules_;
    SceneMonitorPort* scene_;
    FoodDetectorPort* food_;
    SceneJudgingPort* judge_;                            // nullopt = 纯状态机
    int judge_every_;
    int tick_ = 0;                                       // 复核 cadence 计数
    std::unique_ptr<prosophor::nr::domain::StateVoter> voter_;      // 仅 judge_ 存在时创建
};

}  // namespace prosophor::nr::app
