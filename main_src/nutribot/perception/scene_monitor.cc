// SceneMonitor 实现 —— 帧差运动占比 + FSM 推进（详见头注释）
#include "perception/scene_monitor.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "common/log_wrapper.h"

namespace prosophor::nr::infra {

namespace {

// 帧 -> 图片路径（帧视图为 ImageFrame；空帧视为无图）。
// 与 vl_food_detector.cc 的同名 helper 刻意各留一份：各有各的边界单元，
// 不为此抽公共头。
std::string ImagePathOf(prosophor::nr::app::FrameRef frame) {
    if (frame == nullptr) return {};
    return static_cast<const prosophor::nr::app::ImageFrame*>(frame)->image_path;
}

}  // namespace

SceneMonitor::SceneMonitor(
    const prosophor::nr::domain::SceneHoldThresholds& thresholds,
    const MotionConfig& motion)
    : ticker_(thresholds), motion_(motion) {}

bool SceneMonitor::MeasureMotion(const std::string& image_path) {
    const cv::Mat gray = cv::imread(image_path, cv::IMREAD_GRAYSCALE);
    if (gray.empty()) return false;

    cv::Mat small;
    cv::resize(gray, small,
               cv::Size(motion_.small_width, motion_.small_height), 0.0, 0.0,
               cv::INTER_AREA);

    if (has_prev_) {
        cv::Mat diff;
        cv::absdiff(prev_small_, small, diff);
        cv::Mat mask;
        cv::threshold(diff, mask, motion_.pixel_threshold, 255, cv::THRESH_BINARY);
        last_ratio_ = static_cast<double>(cv::countNonZero(mask)) /
                      (static_cast<double>(motion_.small_width) * motion_.small_height);
    } else {
        last_ratio_ = 0.0;   // 首帧无参照："未知"不等于"有运动"，按静止记
    }
    prev_small_ = small;
    has_prev_ = true;
    has_measurement_ = true;

    LOG_DEBUG("nutribot: [场景] 运动={:.4f} 帧={}", last_ratio_, image_path.c_str());
    return true;
}

std::string SceneMonitor::update(prosophor::nr::app::FrameRef frame, bool has_faces,
                                 bool has_foods) {
    const std::string path = ImagePathOf(frame);
    if (path.empty()) {
        return std::string(ticker_.label());   // 无帧：不推进（保持当前判定）
    }

    if (path != handled_key_) {
        handled_key_ = path;
        if (!MeasureMotion(path)) {
            ++decode_failures_;
            if (decode_failures_ == 1 || decode_failures_ % 20 == 0) {
                LOG_WARN("nutribot: 场景帧读取失败（连续 {} 次）：{}",
                         decode_failures_, path.c_str());
            }
            return std::string(ticker_.label());   // 坏帧不推动状态机
        }
        decode_failures_ = 0;
    }
    // 同帧二次驱动（App 场景节拍 + MealMonitor::Tick 内部）：ticker 幂等短路
    const prosophor::nr::domain::SceneState state =
        ticker_.observe(path, {last_ratio_, has_faces, has_foods});
    return std::string(prosophor::nr::domain::scene_label(state));
}

void SceneMonitor::force(std::string_view label) {
    if (const auto state = prosophor::nr::domain::scene_from_label(label)) {
        ticker_.force(*state);
    }
}

std::string SceneMonitor::motion_level() const {
    if (!has_measurement_) return "unknown";
    using prosophor::nr::domain::SceneStateMachine;
    if (last_ratio_ > SceneStateMachine::MOTION_HIGH) return "high";
    if (last_ratio_ < SceneStateMachine::MOTION_LOW) return "low";
    return "mid";
}

}  // namespace prosophor::nr::infra
