// 饭点表：把 "HH:MM" 窗口解析成当日分钟区间，供主循环判定"现在是不是饭点"
//
// 纯逻辑、零依赖，可独立单测。不复用 core 的 CronScheduler —— 这里要的是
// "区间查询"（给定分钟数问在不在窗口内、在哪个窗口），不是"到点回调"；
// 为查区间引入定时器框架不划算，且 nutribot 单测与 core 天然隔离。
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prosophor::nr::app {

// 解析 "HH:MM" / "H:MM"（允许两侧空白）-> 当日分钟数 [0, 1439]；
// 越界/非数字/缺冒号 -> nullopt（配置错就丢窗口，不猜）
std::optional<int> ParseClockMinutes(std::string_view hhmm);

// 一个饭点窗口。约定 start < end（不支持跨零点窗口 —— 三餐场景无此需求，
// 写反的配置在 AddWindow 处拒绝，而不是悄悄当成空窗口）
struct MealWindow {
    std::string name;              // 中文名（"午餐"），问候语直接用它
    int start_minutes = 0;
    int end_minutes = 0;

    // 含头不含尾：11:30:00 在窗口内，13:30:00 已出窗口
    bool contains(int minutes_of_day) const {
        return minutes_of_day >= start_minutes && minutes_of_day < end_minutes;
    }
};

class MealSchedule {
public:
    // 添加窗口；任一段非法（名字空/解析失败/start >= end）返回 false 并不添加
    // —— 配置错误由调用方记日志，表本身只保留可用部分
    bool AddWindow(const std::string& name, const std::string& start,
                   const std::string& end);

    // 该分钟数所处的窗口（重叠时取先添加者）；不在任何窗口 -> nullopt
    std::optional<MealWindow> Lookup(int minutes_of_day) const;

    const std::vector<MealWindow>& windows() const { return windows_; }
    bool empty() const { return windows_.empty(); }

private:
    std::vector<MealWindow> windows_;
};

}  // namespace prosophor::nr::app
