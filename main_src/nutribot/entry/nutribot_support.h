// nutribot_support：文案拼接、路径解析、视觉就绪等待与本地时间
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "prosophor_core/public/config.h"
#include "domain/food.h"

namespace prosophor {
class AgentEngine;
}

namespace prosophor::nr::nutribot {

/// 等待 aidgen 视觉链路就绪的轮询结果
struct VisionWaitResult {
    bool ready = false;    // 视觉链路可用
    bool aborted = false;  // abort 回调要求中止
    int waited_ms = 0;     // 实际等待时长
    std::string error;     // 未就绪原因（ready=false 且未 abort）
};

/// WaitForAidgenVision - 轮询 aidgen provider 状态直到视觉装载完成/失败/超时
/// @param engine 引擎实例
/// @param abort 每拍中止判定；为空表示不可中止
/// @param log_tag 进度日志前缀（如 "nutribot"）
VisionWaitResult WaitForAidgenVision(prosophor::AgentEngine& engine,
                                     const std::function<bool()>& abort = {},
                                     const std::string& log_tag = "nutribot");

std::string JoinFoodNames(const std::vector<prosophor::nr::domain::FoodItem>& foods);
std::string ResolveNutritionFile(const prosophor::NutribotConfig& cfg);
/// 本地时间当日分钟数（饭点判定；localtime_r 线程安全）
int LocalMinutesOfDay();

}  // namespace prosophor::nr::nutribot
