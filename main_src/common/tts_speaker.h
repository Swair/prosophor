// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/noncopyable.h"

#include <string>
#include <memory>
#include <functional>

namespace prosophor {

/// TtsSpeaker: edge-tts 语音合成播放器
/// 子进程调用 edge-tts 生成 WAV，通过回调通知应用层播放
class TtsSpeaker : public Noncopyable {
 public:
    static TtsSpeaker& GetInstance();

    /// 初始化（验证 edge-tts 可用）
    void Initialize();

    /// 播放文字（异步，不阻塞）
    void Speak(const std::string& text);

    /// 是否正在合成/播放
    bool IsSpeaking() const;

    /// 设置语音（如 "zh-CN-XiaoxiaoNeural"）
    void SetVoice(const std::string& voice);

    /// 获取当前语音
    const std::string& GetVoice() const { return voice_; }

    /// 合成完成回调（传入 wav 文件路径）
    using OnSynthesizedCallback = std::function<void(const std::string& wav_path)>;
    void SetOnSynthesized(OnSynthesizedCallback cb);

 private:
    TtsSpeaker() = default;

    /// 同步合成：text → wav 文件路径
    std::string Synthesize(const std::string& text);

    /// 异步合成线程
    void SpeakAsync(const std::string& text);

    std::string voice_ = "zh-CN-XiaoxiaoNeural";
    OnSynthesizedCallback on_synthesized_;
    bool speaking_ = false;
};

}  // namespace prosophor
