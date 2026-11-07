// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/noncopyable.h"

#include <memory>
#include <string>

namespace prosophor {

namespace voice {
class VoiceInteraction;
}  // namespace voice

/// VoiceEngine: SDL 形态的语音门面。
///
/// 实际编排在 voice_interaction::VoiceInteraction：
///   - VAD：vad::WebRtcVad（WebRTC 算法在 rtc_wrapper/rtc）
///   - 播放：MediaEngineAudioOutput（下混/重采样到 16k → VoiceChannel）
///   - 采集：media_engine::VoiceChannel 回调直接 FeedFrame
///   - provider 在 Init 时按配置创建（桌面端不做模型预热）
class VoiceEngine : public Noncopyable {
public:
    static VoiceEngine& GetInstance();
    ~VoiceEngine();

    /// Poll for the latest transcription result (interim or final, thread-safe).
    std::string PollAsrLatest();

    // ── Audio Capture (mic) — delegates to VoiceChannel ──
    void StartCapture();
    void StopCapture();

    // ── Audio Playback (TTS → VoiceChannel → AudioStreamer) ──
    void Speak(const std::string& text,
               const std::string& backend = "edge-tts",
               const std::string& voice = "zh-CN-XiaoxiaoNeural");

private:
    VoiceEngine();

    std::unique_ptr<voice::VoiceInteraction> interaction_;
};

}  // namespace prosophor
