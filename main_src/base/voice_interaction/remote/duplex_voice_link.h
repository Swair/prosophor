// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// 云端全双工语音通道接口（iiix_ws / xiaozhi 流程参考）。
//
// 设备只负责采集与播放，VAD/ASR/TTS 全在远端：
//   Idle → Connecting → Listening → UserSpeaking → Thinking → Speaking → Listening
// 远端用 listen start/stop 与 tts start/stop 控制上下行（对应 xiaozhi 协议），
// 本接口只表达状态与 PCM 流向，不绑定 WebSocket / Opus / 具体服务。
//
// 当前 VoiceInteraction 走本地闭环；需要接入云端时，由应用层实现本接口并
// 与 VoiceInteraction 并排编排（本地闭环与远端通道二选一）。

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace prosophor::voice {

enum class DuplexState {
    kIdle,
    kConnecting,
    kListening,
    kUserSpeaking,
    kThinking,
    kSpeaking,
    kClosed,
    kError,
};

struct DuplexLinkConfig {
    std::string endpoint;
    int input_sample_rate = 16000;    // PushMicPcm 契约：16k 单声道 int16
    int output_sample_rate = 24000;   // 播放提示；实际以回调参数为准
    int output_channels = 1;
    bool allow_barge_in = false;      // 播放期是否允许用户打断
};

class DuplexVoiceLink {
public:
    struct Callbacks {
        std::function<void(DuplexState)> on_state;
        std::function<void(const std::vector<int16_t>& pcm, int sample_rate, int channels)>
            on_playback_pcm;
        std::function<void(const std::string& text, bool is_final)> on_asr_text;
        std::function<void(const std::string& error)> on_error;
    };

    virtual ~DuplexVoiceLink() = default;

    virtual bool Start(const DuplexLinkConfig& config, Callbacks callbacks,
                       std::string& error) = 0;

    /// 采集线程推送麦克风 PCM（16k 单声道 int16）
    virtual void PushMicPcm(const int16_t* pcm, size_t samples) = 0;

    /// 远端 TTS 播放期间闭麦
    virtual void SetMicMuted(bool muted) = 0;

    virtual DuplexState state() const = 0;
    virtual bool IsSpeaking() const = 0;

    /// 幂等退出；实现负责断开连接并 join 收发线程
    virtual void Stop() = 0;
};

}  // namespace prosophor::voice
