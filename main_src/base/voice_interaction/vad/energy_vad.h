// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "vad/vad.h"

namespace prosophor::voice {

/// 纯数学能量 VAD：帧 RMS 高于门限即判为有声。
/// 无模型、无外部依赖，板上常驻回路默认使用。
class EnergyVad final : public IVad {
public:
    explicit EnergyVad(double threshold_dbfs = -38.0);

    bool ProcessFrame(const int16_t* pcm, size_t samples) override;
    void Reset() override {}

private:
    double threshold_dbfs_;
};

/// 帧能量 → dBFS（0 = 满幅；全零返回 -1000）
double FrameRmsDbfs(const int16_t* pcm, size_t samples);

}  // namespace prosophor::voice
