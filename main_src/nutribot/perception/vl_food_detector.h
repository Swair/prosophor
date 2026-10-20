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

#include "common/time_wrapper.h"
#include "domain/food.h"
#include "ports.h"

namespace prosophor::nr::app {

// 看图提问后端：图片路径 + 提问文本 -> 回复文本；不可用返回 nullopt
using VisionQueryBackend = std::function<std::optional<std::string>(
    const std::string& image_path, const std::string& question)>;

// 组装菜品识别提问（纯函数，可单测）。
// candidates 非空 = **闭集模式**：候选菜名进 prompt，模型做"从候选里挑并排序"
// 的选择题；空 = 原开放模式（行为逐字不变）。
//
// 实测对照（2026-10-17，72 张人工标注，Qwen3-VL 8B，Top-5 命中口径）：
//   开放命名       Top-1 严格只有 2/72
//   闭集 66 选 1   Top-1 严格 22/72 —— 8B 上闭集明显更好，**产品口径 = 闭集**。
// 但闭集对非餐桌画面会"稳定硬选"：连续多帧返回同一组候选（实测空画面报出
// 5 道菜并触发假提醒）。时间一致性门控挡不住稳定幻觉，故另加独立的存在性
// 二分类校验（FoodPresenceQuestion/ParseFoodPresenceReply，装配层按稳定集合
// 变化时触发），而不是在识别 prompt 里加"没有菜就输出 []"——A/B 实测那会
// 伤召回（12 张单菜图 Top-1 3/12 -> 0/12）。
std::string build_food_detect_prompt(const std::vector<std::string>& candidates = {});

// 菜品存在性二分类提问：与识别 prompt 分开发，避免候选列表诱导"必须选一个"。
// 实测：菜图答"有"、沙滩/非餐桌图答"没有"。
const char* FoodPresenceQuestion();

// 解析存在性回答：有 -> true；没有 -> false；无法判断 -> nullopt。
// 调用方对 nullopt 建议 fail-open（后端异常时不误伤真实上菜）。
std::optional<bool> ParseFoodPresenceReply(const std::string& reply);

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
    prosophor::SteadyClock::TimePoint last_query_at_{};
    bool has_queried_ = false;
    int query_count_ = 0;
};

}  // namespace prosophor::nr::app
