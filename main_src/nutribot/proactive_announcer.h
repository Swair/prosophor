// 主动播报规划：把感知结果翻译成"这一轮该说哪句话"（纯逻辑，可单测）
//
// 三类话源与各自策略：
//   禁忌提醒 —— 同"成员|菜品"只播一次；节流窗口内播不出去则挂起，
//               窗口过后补播（宁可迟到，不可哑掉）；菜撤桌后登记清除，
//               重新上桌能再次提醒（新的一餐）
//   报菜名   —— 集合连续两轮一致且与上次不同才播（VL 单轮抖动不播报）；
//               顺序无关（按名排序比较）；让位给提醒时放弃本集合的播报
//   饭点问候 —— 由调用方在"进入窗口"时调一次；被节流则调用方下轮重试
//
// FLICKER 防线：桌上状态（含"菜撤桌"清理）只在**连续两轮集合一致**时采纳，
// 单次查询的抖动不会改变任何登记 —— 否则 VL 偶尔漏认一道菜就会把提醒登记
// 清掉又重建，造成重复播报。
//
// 契约：调用方只在"能立刻播报"时调用（未在播报中）；Plan* 返回非空即应
// 立即 Speak —— 记账以本函数返回那一刻为准，调用方弃播则这条话丢失。
#pragma once

#include <chrono>
#include <deque>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "events.h"
#include "meal_journal.h"

namespace prosophor::nr::app {

using Clock = std::chrono::steady_clock;

struct AnnouncerTuning {
    int min_gap_ms = 10000;   // 两次播报最小间隔（含用户话轮回复）
};

class ProactiveAnnouncer {
public:
    explicit ProactiveAnnouncer(AnnouncerTuning tuning = {});

    // 感知结果 -> 本轮该播的话；空 = 本轮不播（稍后再喂下一轮）
    std::string PlanPerception(const prosophor::nr::domain::ScenePackage& pkg,
                               Clock::time_point now);

    // 饭点进入 -> 问候语；空 = 节流中，调用方保留"待问候"状态下轮重试
    std::string PlanMealEntry(const std::string& meal_name, Clock::time_point now);

    // 餐后小结 -> 播报文本；空 = 节流中，调用方挂起等下轮重试
    std::string PlanMealSummary(const MealSummary& summary, Clock::time_point now);

    // 用户话轮播报后刷新节流基准（主动播报不与刚回答完的话抢麦）
    void NoteConversationSpeech(Clock::time_point now);

    struct Stats {
        int reminders_spoken = 0;      // 已播报的提醒条数（事件计数，非话次）
        int food_announcements = 0;    // 已播报的报菜名次数
        int meal_announcements = 0;    // 饭点问候次数
        int meal_summaries = 0;        // 餐后小结次数
        int suppressed_by_gap = 0;     // 因节流推迟的规划次数
        int pending_reminders = 0;     // 当前挂起条数（快照）
    };
    Stats stats() const;

private:
    bool GapElapsed(Clock::time_point now) const;
    void NoteSpoken(Clock::time_point now);
    // 撤桌登记清理：挂起提醒与已播登记中，菜品不在桌上的条目作废
    void DropVanished(const std::set<std::string>& food_names);

    AnnouncerTuning tuning_;
    std::optional<Clock::time_point> last_spoke_at_;   // nullopt = 尚可立即播报

    std::deque<prosophor::nr::domain::ReminderEvent> pending_reminders_;
    std::set<std::pair<std::string, std::string>> notified_;   // {成员, 菜品} 已挂起/已播

    std::string prev_food_key_;       // 上一轮的菜品集合指纹（稳定性判定）
    std::string settled_food_key_;    // 已采纳（连续两轮一致）的集合指纹
    std::string announced_food_key_;  // 已播报过的集合指纹（空 = 尚未报过）

    Stats stats_;
};

/// 菜品集合指纹：按名排序去重后以 \n 连接 —— 顺序无关、内容敏感（可单测）
std::string FoodSetKey(const std::vector<prosophor::nr::domain::FoodItem>& foods);

}  // namespace prosophor::nr::app
