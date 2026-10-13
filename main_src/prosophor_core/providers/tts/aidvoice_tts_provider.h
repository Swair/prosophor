// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// AidVoice TTS Provider：本地 NPU 语音合成直连（官方 aidvoice-sdk）
//
// 与 aidgen_provider 同为仓库的 SDK 接入点：头文件不引 SDK 头（不透明句柄
// 前置声明），aidvoice.hpp 只在本 .cc 出现 —— 非 nutribot 形态靠 CMake 过滤
// 本 .cc 即可在无 SDK 环境编译。
//
// 设计取舍（2026-10-12 板上实测）：
//   - 只合成不播放：set_microphone_id(-1)，播放归调用方的音频层（nutribot
//     audio_io）—— 播报时机与"播报期闭麦"必须由主循环统一控制
//   - 不落盘：不调 set_audio_path —— 回调 audio_data 已带完整 PCM；调用它
//     会让 SDK 每句自动写 WAV 文件，长期运行堆满磁盘
//   - 输出固定 48kHz 单声道；回调 float 为归一化 [-1,1]（实测 peak≈0.35），
//     转 int16 用 clamp(v*32767)
//   - SDK 1.5.0 无发音人参数（单女声），可调项只有语速 set_speed
//
// 线程契约：Synthesize 必须单线程串行调用（SDK 内部单队列）；SDK 回调在内部
// 线程执行，桥接类只累积数据并 notify。

#include <memory>
#include <string>

#include "providers/tts/tts_provider.h"

namespace prosophor {

// aidvoice 运行时句柄（TTS 实例 + 回调桥接，定义于 .cc）
struct AidVoiceTtsHandle;

struct AidVoiceTtsConfig {
    std::string model_dir;    // 模型目录（config.json 里的 type 决定中/英模型）
    double speed = 1.0;       // 语速倍率（>0；1.0 = 模型原生速度）
    int timeout_ms = 30000;   // 单次合成等待上限（长句 + 板载负载需要余量）
};

class AidVoiceTtsProvider final : public TtsProvider {
public:
    explicit AidVoiceTtsProvider(AidVoiceTtsConfig cfg);
    ~AidVoiceTtsProvider() override;

    std::string GetProviderName() const override { return "aidvoice"; }

    /// 文本 → PCM（阻塞至合成完成或超时）。
    /// 请求里的 voice 被忽略（SDK 无发音人），sample_rate 被忽略（模型固定
    /// 48000）—— 响应里回真实采样率，调用方按响应打开播放设备
    TtsResponse Synthesize(const TtsRequest& request) override;

    /// 预创建 + init（启动期调用，把约 2.4s 装载开销挪出首次播报）
    bool WarmUp(std::string& error);

    /// 显式销毁 SDK 实例（main 的 std::_Exit 跳过静态析构，退出前必须调用）
    void Destroy();

private:
    bool EnsureReady(std::string& error);

    AidVoiceTtsConfig cfg_;
    std::unique_ptr<AidVoiceTtsHandle> handle_;
};

}  // namespace prosophor
