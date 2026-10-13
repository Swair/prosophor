// 场景感知端口实现：帧差运动占比 + core FSM（opencv 解码，无 NPU 消耗）
//
// 职责切分（同 VlFoodDetector 的边界约定）：
//   本文件只做"从一帧图算出运动占比"这件 infra 事（解码/降采样/差分），
//   状态机与阈值全部在纯层 scene.h（可单测）。帧率与时间标定由装配层负责：
//   每来一个新相机帧驱动一次（~3s），按 MakeHoldThresholds 折算的阈值推进。
//
// 帧差口径（板上标定入口 MotionConfig）：
//   灰度解码 -> 降到 160x120(INTER_AREA，块平均顺带压 JPEG/传感器噪声)
//   -> 与上一帧 absdiff -> 像素差 > 12/255 二值化 -> 计占比。
//   静态桌面的噪声占比经验 <0.3%，低于 MOTION_LOW=1%，"静止"判据不会被推翻；
//   auto-exposure 的整帧亮度漂移(常见 ±5)也落在阈值下。板上若见静态 ratio
//   偏高/偏低，优先调 pixel_threshold，其次动降采样尺寸。
#pragma once

#include <string>

#include <opencv2/core.hpp>

#include "ports.h"
#include "scene.h"

namespace prosophor::nr::infra {

struct MotionConfig {
    int small_width = 160;       // 降采样目标宽（固定尺寸：两端同域，无分辨率分支）
    int small_height = 120;
    int pixel_threshold = 12;    // 像素差二值化阈值（0-255）
};

class SceneMonitor final : public prosophor::nr::app::SceneMonitorPort {
public:
    explicit SceneMonitor(
        const prosophor::nr::domain::SceneHoldThresholds& thresholds = {},
        const MotionConfig& motion = {});

    // 处理一帧（内部算运动）并推进 FSM；同帧重复调用不重复解码、不重复计数
    std::string update(prosophor::nr::app::FrameRef frame, bool has_faces,
                       bool has_foods) override;
    void force(std::string_view label) override;
    // "high" / "low" / "mid" / "unknown"（未测过至少一次帧差 -> unknown）
    std::string motion_level() const override;

    // 诊断（板上标定与统计行用）
    double last_motion_ratio() const { return last_ratio_; }
    int decode_failures() const { return decode_failures_; }
    int transitions() const { return ticker_.transitions(); }

private:
    // 解码当前帧并与上一帧求运动占比；失败返回 false（不更新 prev、不喂 FSM）
    bool MeasureMotion(const std::string& image_path);

    prosophor::nr::domain::SceneTicker ticker_;
    MotionConfig motion_;

    cv::Mat prev_small_;              // 上一帧 160x120 灰度（约 19KB）
    bool has_prev_ = false;
    bool has_measurement_ = false;    // 是否算过至少一次帧差
    double last_ratio_ = 0.0;
    std::string handled_key_;         // 已处理过的帧路径（同帧二次驱动短路）
    int decode_failures_ = 0;
};

}  // namespace prosophor::nr::infra
