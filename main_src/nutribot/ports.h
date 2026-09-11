// 应用层端口（Ports）：用例依赖的抽象接口，由 infrastructure 实现
//
// 移植自 Python application/ports.py。本层只依赖 core/ 与 std，
// 可脱离硬件（NPU/摄像头/LLM）单元测试。
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "food.h"
#include "repositories.h"
#include "scene.h"

namespace prosophor::nr::app {

// 帧句柄：感知实现（aidlite/camera）的输入载体。
// 具体帧视图由 infrastructure 定义（见 ImageFrame），应用层只透传、不解引用
// —— 与 Python 把 cv2 frame 当不透明对象同语义。
using FrameRef = const void*;

// 帧视图（第一阶段）：一帧图像 = 磁盘上的一个图片文件。
// AidGen VL 本就按路径取图（provider 内部 OpenCV 解码），故采集侧落盘一帧、
// 感知侧按路径读；换采集源（摄像头/视频文件）不动端口。像素缓冲视图待真实
// 摄像头接入时再扩展 —— 届时本结构加字段，端口签名不变。
struct ImageFrame {
    std::string image_path;
};

// 单张人脸的识别结果（应用层值对象；对应 Python FaceObservation）
struct FaceObservation {
    std::optional<std::string> name;         // 未识别为 nullopt
    double similarity = 0.0;
    std::array<int, 4> box{};                // (x, y, w, h)
    std::vector<std::string> health_tags;
    std::vector<std::string> taboo_foods;

    bool known() const { return name.has_value() && !name->empty(); }
};

// 人脸识别端口：检测 -> 对齐 -> 特征 -> 成员匹配
class FaceRecognitionPort {
public:
    virtual ~FaceRecognitionPort() = default;
    // 返回在场人脸列表；按帧节流特征提取由实现负责
    virtual std::vector<FaceObservation> detect_and_recognize(
        FrameRef frame, prosophor::nr::domain::MemberRepository& repository,
        double threshold, int min_size) = 0;
    // 释放 NPU 解释器等资源（QNN 进程退出需显式销毁）
    virtual void release() = 0;
};

// 菜品检测端口（Phase 3）：帧 -> 领域菜品列表
class FoodDetectorPort {
public:
    virtual ~FoodDetectorPort() = default;
    virtual std::vector<prosophor::nr::domain::FoodItem> detect_domain(FrameRef frame) = 0;
};

// 场景状态端口：应用层唯一依赖的"场景机"抽象。
// 阶段 2 由 infrastructure 的 SceneMonitor（帧差运动 + core FSM）实现；
// 单测注入 Fake。
class SceneMonitorPort {
public:
    virtual ~SceneMonitorPort() = default;
    // 处理一帧（内部算运动）并推进 FSM，返回状态中文标签
    virtual std::string update(FrameRef frame, bool has_faces) = 0;
    // 判决侧确认后强制同步 FSM（幂等，UNKNOWN 忽略）
    virtual void force(std::string_view label) = 0;
    // 运动档位: "high" / "low" / "mid" / "unknown"（供 LLM 判据）
    virtual std::string motion_level() const = 0;
};

// 场景判决结果（LLM/外部判据输出）
// state 枚举限定；confidence/reason 仅日志调试，不参与切换逻辑
struct SceneJudgement {
    prosophor::nr::domain::SceneState state;
    double confidence = 0.0;
    std::string reason;
};

// LLM 判据输入（对应 Python 的 observation dict）
struct SceneJudgeObservation {
    std::string current_state;                 // 当前状态中文标签
    std::vector<std::string> members;          // 在场成员名
    std::vector<std::string> foods;            // 桌上菜品名（排序去重由用例层做）
    std::string motion_level;                  // high/low/mid/unknown
};

// 场景判决端口（Phase 5）：复核状态机初判。
// 不可用/超时/无法判定 -> 一律返回 nullopt（调用方回退 FSM，不阻塞）
class SceneJudgingPort {
public:
    virtual ~SceneJudgingPort() = default;
    virtual std::optional<SceneJudgement> judge(
        const SceneJudgeObservation& observation) = 0;
    virtual void release() = 0;
};

// 对话端口（Phase 6）：提问并听取回答（ASR+TTS 实现；当前终端实现）
class DialoguePort {
public:
    virtual ~DialoguePort() = default;
    // 返回用户回答文本；超时/无回应返回 nullopt
    virtual std::optional<std::string> ask(const std::string& question) = 0;
};

}  // namespace prosophor::nr::app
