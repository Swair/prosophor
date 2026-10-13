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

// 组装菜品识别提问（纯函数，可单测）。
// candidates 非空 = **闭集模式**：候选菜名进 prompt，模型做"从候选里挑并排序"
// 的选择题；空 = 原开放模式（行为逐字不变）。
//
// 实测对照（2026-10-13，72 张人工标注，Qwen3-VL 4B，Top-5 命中口径）：
//   开放命名       Top-1 严格 17% / 放宽 22% / Top-5 25%；非餐桌图不误报
//   闭集 66 选 1   Top-1 严格 14% / Top-5 24%；非餐桌图会"抄列表"误报 4 道菜
//   → 长候选列表让小 VL 模型退化成从列表里抄段（多张图返回相同的列表片段），
//     闭集反而不如开放；**产品口径 = 开放命名 + 命中知识库即归一**（见
//     NutritionEnrichingDetector），闭集模式保留为基准对照。
//   → prompt 工程的两种形态都到顶（~25%）：85% 指标要靠训练出来的闭集检测器，
//     本文件上方的逐菜基准（selftest [6] 自动评分）就是那条线的验收尺。
std::string build_food_detect_prompt(const std::vector<std::string>& candidates = {});

// 解析模型回复为菜品列表；JSON 非法/空 -> 空列表。
// candidates 非空 = 闭集模式：只接受候选内的名字（精确或包含匹配，归一为候选
// 的规范名），候选外的名字一律丢弃（此时"回吐守卫"由候选集天然承担）
std::vector<prosophor::nr::domain::FoodItem> parse_food_detect_response(
    const std::string& text, const std::vector<std::string>& candidates = {});

// 闭集匹配（纯函数）：在候选里找 raw 的最佳匹配（精确 -> 最长包含，两侧≥2 字符），
// 返回候选中的规范名；无匹配 -> nullopt。同时被解析层与基准评测使用。
std::optional<std::string> MatchCandidateName(
    const std::string& raw, const std::vector<std::string>& candidates);

// VL 菜品识别（backend 为空时 detect_domain 恒返回空）
class VlFoodDetector final : public FoodDetectorPort {
public:
    explicit VlFoodDetector(VisionQueryBackend backend,
                            double min_interval_seconds = 3.0,
                            std::vector<std::string> candidates = {});

    std::vector<prosophor::nr::domain::FoodItem> detect_domain(
        FrameRef frame) override;

    // 模型实际调用次数（节流证据；demo 打印、测试断言用）
    int query_count() const { return query_count_; }

    // 闭集候选（空 = 开放模式；诊断/日志用）
    const std::vector<std::string>& candidates() const { return candidates_; }

private:
    // 距上次调用是否已过节流窗口
    bool IntervalElapsed() const;

    VisionQueryBackend backend_;
    double min_interval_seconds_ = 3.0;
    std::vector<std::string> candidates_;   // 非空 = 闭集模式（候选进 prompt/解析归一）
    std::string last_frame_key_;                               // 上次识别的帧
    std::vector<prosophor::nr::domain::FoodItem> last_result_;  // 该帧的识别结果
    std::chrono::steady_clock::time_point last_query_at_{};
    bool has_queried_ = false;
    int query_count_ = 0;
};

}  // namespace prosophor::nr::app
