// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

namespace prosophor::voice {

/// 帧级语音活动检测。VoicePipeline 只依赖此接口：
/// - nutribot 注入 EnergyVad；
/// - SDL 注入 WebRtcVad；nutribot 可选 vad_backend=webrtc。
class IVad {
public:
    virtual ~IVad() = default;

    /// 处理一帧 PCM，返回该帧是否有人声
    virtual bool ProcessFrame(const int16_t* pcm, size_t samples) = 0;

    /// 清空内部状态（采集重启/播报前后调用）
    virtual void Reset() = 0;
};

}  // namespace prosophor::voice
