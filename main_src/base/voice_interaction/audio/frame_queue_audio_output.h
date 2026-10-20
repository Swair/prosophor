// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// 拉取式双工音频输出（sibot SIP 发送回调的抽象）：
//   TTS worker 调 Write 把 PCM 入队（非阻塞）；
//   传输层/设备的 send 回调调 PullFrame 拉一帧，不足补零；
//   send 回调在 PullFrame 返回 false（队列排空）时触发 reListen/ResetUtterance。
//
// 与 ALSA 的阻塞 Drain 不同，这里 Drain 立即返回，播报结束以 QueuedSamples()==0
// 判定；VoiceInteraction::IsSpeaking() 会同时看 pipeline 与队列。

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include "audio/audio_io.h"

namespace prosophor::voice {

class FrameQueueAudioOutput final : public AudioOutput {
public:
    FrameQueueAudioOutput() = default;

    bool Open(int sample_rate, int channels, std::string& error) override;

    /// 入队（任意长度；队列为样本 FIFO，单声道/交错由调用方约定）
    bool Write(const int16_t* data, size_t samples) override;

    /// 非阻塞：实际播放由传输层 PullFrame 驱动
    bool Drain() override { return true; }

    /// 清空队列（退出/打断）
    void Abort() override;
    void Close() override;

    bool IsOpen() const override;
    int sample_rate() const override;
    int channels() const override;

    /// send 回调：拉取一帧；不足补零。返回是否有真实音频。
    bool PullFrame(int16_t* out, size_t samples) override;

    size_t QueuedSamples() const override;

private:
    mutable std::mutex mutex_;
    std::deque<int16_t> queue_;
    int sample_rate_ = 0;
    int channels_ = 1;
};

}  // namespace prosophor::voice
