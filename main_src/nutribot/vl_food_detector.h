// 菜品识别（Port: FoodDetectorPort）—— VL 模型看帧报菜名（Phase 3）
//
// 思路与 LlmSceneJudger 同构：模型后端以 std::function 注入，本文件不碰 SDK
// （aidgen/aidlite/opencv 仍只在 aidgen_provider.cc 出现），故可脱离 NPU 单测。
// 不可用、超时、解析失败一律返回空列表（规则引擎自然不触发）。
//
// 节流是端口实现的责任（与 FaceRecognitionPort 同约定）：MealMonitor 每 tick 无条件
// 调用本端口，而 VL 推理是秒级 —— 同一帧直接命中缓存，异帧受 min_interval 限制。
#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "food.h"
#include "ports.h"

namespace prosophor::nr::app {

// 看图提问后端：图片路径 + 提问文本 -> 回复文本；不可用返回 nullopt
using VisionQueryBackend = std::function<std::optional<std::string>(
    const std::string& image_path, const std::string& question)>;

// 组装菜品识别提问（纯函数，可单测）
std::string build_food_detect_prompt();

// 解析模型回复为菜品列表；JSON 非法/空 -> 空列表
std::vector<prosophor::nr::domain::FoodItem> parse_food_detect_response(
    const std::string& text);

// VL 菜品识别（backend 为空时 detect_domain 恒返回空）
class VlFoodDetector final : public FoodDetectorPort {
public:
    explicit VlFoodDetector(VisionQueryBackend backend,
                            double min_interval_seconds = 3.0);

    std::vector<prosophor::nr::domain::FoodItem> detect_domain(
        FrameRef frame) override;

    // 模型实际调用次数（节流证据；demo 打印、测试断言用）
    int query_count() const { return query_count_; }

private:
    // 距上次调用是否已过节流窗口
    bool IntervalElapsed() const;

    VisionQueryBackend backend_;
    double min_interval_seconds_ = 3.0;
    std::string last_frame_key_;                               // 上次识别的帧
    std::vector<prosophor::nr::domain::FoodItem> last_result_;  // 该帧的识别结果
    std::chrono::steady_clock::time_point last_query_at_{};
    bool has_queried_ = false;
    int query_count_ = 0;
};

}  // namespace prosophor::nr::app
