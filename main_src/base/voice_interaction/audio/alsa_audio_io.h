// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// ALSA 直连的麦克风采集与扬声器播放（nutribot 常驻语音回路专用）。
//
// 本文件是 voice_interaction 内唯一接触 ALSA 的地方；采集与播放各自独立对象
// （板上是两张声卡：采集 PowerConf、播放 M83）。
//
// 设备名用 plughw:CARD=xxx,DEV=n 形式 —— 卡序号会随插拔重排，卡名稳定
// （2026-10-12 实测：板上有 PulseAudio/PipeWire 在跑，ALSA 直连仍可用）。
// plughw 插件层负责采样率/格式转换，本层按 16k/48k 单声道 S16 读写即可。
//
// 线程约定：
//   - 采集：Read 走 poll 带超时 —— 调用线程（采集线程）周期性醒来查退出标志，
//     不用阻塞式 readi。Close 必须在线程 join 之后调用。
//   - 播放：Write 分块写入并检查 Abort 标志 —— 退出路径调 Abort 后，正在
//     播放的 Write 在一块（约 200ms）内返回，避免跨线程操作 ALSA 句柄。
//
// 已知坑（2026-10-12 实测）：设备空闲后开流的头几帧可能是数字静音
// （peak≈3/32767），由调用方丢弃开头若干帧（voice.warmup_discard_frames）。

#include <alsa/asoundlib.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "audio/audio_io.h"

namespace prosophor::voice {

struct CaptureConfig {
    std::string device;         // plughw:CARD=PowerConf,DEV=0
    int sample_rate = 16000;
    int channels = 1;
    int frame_samples = 480;    // 30ms @16k（与 VAD 帧长一致）
    int latency_us = 300000;    // 设备环形缓冲 300ms（板上负载抖动大，太小会溢出）
};

struct PlaybackConfig {
    std::string device;         // plughw:0,0
    int sample_rate = 48000;    // MeloTTS 固定 48k（按 TtsResponse.sample_rate 传）
    int channels = 1;
    int latency_us = 300000;
};

class AlsaAudioInput final : public AudioInput {
public:
    explicit AlsaAudioInput(CaptureConfig cfg);
    ~AlsaAudioInput() override;

    bool Open(std::string& error) override;
    int Read(int16_t* out, int max_samples, int timeout_ms) override;
    void Mute(bool on) override { muted_.store(on); }
    bool IsMuted() const override { return muted_.load(); }
    void Close() override;
    bool IsOpen() const override { return pcm_ != nullptr; }

private:
    CaptureConfig cfg_;
    snd_pcm_t* pcm_ = nullptr;
    int frame_samples_ = 480;
    std::atomic<bool> muted_{false};
};

class AlsaAudioOutput final : public AudioOutput {
public:
    explicit AlsaAudioOutput(PlaybackConfig cfg);
    ~AlsaAudioOutput() override;

    bool Open(int sample_rate, int channels, std::string& error) override;
    bool Write(const int16_t* data, size_t samples) override;
    bool Drain() override;
    void Abort() override { abort_.store(true); }
    void Close() override;
    bool IsOpen() const override { return pcm_ != nullptr; }
    int sample_rate() const override { return cfg_.sample_rate; }
    int channels() const override { return cfg_.channels; }

private:
    PlaybackConfig cfg_;
    snd_pcm_t* pcm_ = nullptr;
    std::atomic<bool> abort_{false};
};

}  // namespace prosophor::voice
