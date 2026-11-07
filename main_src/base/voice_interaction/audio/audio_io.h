// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace prosophor::voice {

/// 采集输入端口：ALSA 直连（nutribot）或应用自定义（如 media_engine 适配）。
/// 线程约定：Read 由采集线程调用；Close 必须在线程 join 之后。
class AudioInput {
public:
    virtual ~AudioInput() = default;

    virtual bool Open(std::string& error) = 0;

    /// >0 = 样本数；0 = 超时/无数据/静音期丢帧；-1 = 错误
    virtual int Read(int16_t* out, int max_samples, int timeout_ms) = 0;

    /// 播报期闭麦：丢帧但保持流排空（不 stop/reopen，避免数字静音坑复现）
    virtual void Mute(bool on) = 0;
    virtual bool IsMuted() const = 0;

    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;
};

/// 播放输出端口：由 VoiceInteraction 按 TTS 响应采样率打开并阻塞写入。
class AudioOutput {
public:
    virtual ~AudioOutput() = default;

    virtual bool Open(int sample_rate, int channels, std::string& error) = 0;

    /// 阻塞写完（分块检查 Abort）；Abort 后返回 false
    virtual bool Write(const int16_t* data, size_t samples) = 0;

    /// 等到全部播完（播报结束判定）；Abort 后立即返回 false
    virtual bool Drain() = 0;

    /// 请求中断当前/后续写入（退出路径用；线程安全）
    virtual void Abort() = 0;

    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;

    /// 当前输出格式；未打开时返回 0
    virtual int sample_rate() const { return 0; }
    virtual int channels() const { return 0; }

    /// 确保以指定格式打开；已用相同格式打开时直接返回 true。
    /// 默认实现：格式不同则 Close 后重新 Open；适配器可覆盖。
    virtual bool EnsureOpen(int rate, int channel_count, std::string& error) {
        if (IsOpen() && sample_rate() == rate && channels() == channel_count) {
            return true;
        }
        Close();
        return Open(rate, channel_count, error);
    }

    // ── 拉取式双工传输（SIP/设备 send 回调）──
    /// 由传输层的发送回调调用：拉取一帧播放 PCM；不足补零。
    /// 返回是否有真实音频。推模式输出（ALSA/media_engine）默认 0/不支持。
    virtual bool PullFrame([[maybe_unused]] int16_t* out,
                           [[maybe_unused]] size_t samples) {
        return false;
    }

    /// 尚未被传输层取走的播放样本数（停止/闭麦判定用）
    virtual size_t QueuedSamples() const { return 0; }
};

}  // namespace prosophor::voice
