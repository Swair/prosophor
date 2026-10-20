// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// VAD 选择集中在这里：读取 voice.vad_backend，
//   "energy" -> EnergyVad
//   "webrtc" -> WebRtcVad（仅含 WebRTC 算法的构建）
//   "auto"   -> default_webrtc 指定的形态默认
//
// WebRTC VAD 的 C 算法在 rtc_wrapper/rtc，所有形态都会编译。

#include <memory>

#include "vad/energy_vad.h"
#include "vad/webrtc_vad.h"
#include "voice_interaction/voice_config.h"

namespace prosophor::voice {

inline std::unique_ptr<IVad> CreateVad(const VoiceConfig& config,
                                       bool default_webrtc) {
    if (config.vad_backend == "webrtc" ||
        (config.vad_backend == "auto" && default_webrtc)) {
        auto vad = std::make_unique<WebRtcVad>();
        vad->SetThreshold(static_cast<float>(config.webrtc_vad_mode));
        return vad;
    }
    return std::make_unique<EnergyVad>(config.vad_rms_dbfs);
}

}  // namespace prosophor::voice
