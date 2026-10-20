// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "rtc_wrapper/rtc/webrtc_vad.h"
#include "vad/vad.h"

#include <cstdint>

namespace prosophor::voice {

/// WebRTC VAD 包装（GMM，默认 mode 3）—— SDL 形态默认 VAD。
/// 与 EnergyVad 同属 IVad 的两个策略实现；C 算法在 rtc_wrapper/rtc/。
class WebRtcVad final : public IVad {
public:
    static constexpr int kSampleRate = 16000;
    static constexpr int kFrameSize  = 480;  // 30ms @ 16kHz

    WebRtcVad();
    ~WebRtcVad() override;

    /// Process one frame (480 int16 samples @ 16kHz).
    /// Returns true if voice activity is detected.
    bool ProcessFrame(const int16_t* data, size_t samples) override;

    /// Set VAD aggressiveness threshold (0=quality ~ 3=very aggressive).
    void SetThreshold(float level);

    /// Reset internal state (call on capture restart).
    void Reset() override;

private:
    VadInst* handle_ = nullptr;
    int mode_ = 3;
};

}  // namespace prosophor::voice
