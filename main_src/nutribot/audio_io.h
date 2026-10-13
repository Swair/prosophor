// 音频 I/O：ALSA 直连的麦克风采集与扬声器播放（nutribot 常驻语音回路专用）
//
// 本文件是 nutribot 内唯一接触 ALSA 的地方；采集与播放各自独立对象
// （板上是两张声卡：采集 PowerConf、播放 M83）。
//
// 设备名用 plughw:CARD=xxx,DEV=n 形式 —— 卡序号会随插拔重排，卡名稳定
// （2026-10-12 实测：板上有 PulseAudio/PipeWire 在跑，ALSA 直连仍可用）。
// plughw 插件层负责采样率/格式转换，本层按 16k/48k 单声道 S16 读写即可。
//
// 线程约定：
//   - 采集：ReadFrame 走 poll 带超时 —— 调用线程（采集线程）周期性醒来查
//     退出标志，不用阻塞式 readi。Close 必须在线程 join 之后调用。
//   - 播放：Write 分块写入并检查 Abort 标志 —— 退出路径调 Abort 后，正在
//     播放的 Write 在一块（约 200ms）内返回，避免跨线程操作 ALSA 句柄。
//
// 已知坑（2026-10-12 实测）：设备空闲后开流的头几帧可能是数字静音
// （peak≈3/32767），由调用方丢弃开头若干帧（voice.warmup_discard_frames）。
#pragma once

#include <alsa/asoundlib.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace prosophor::nr::infra {

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

class AudioCapture {
public:
    AudioCapture() = default;
    ~AudioCapture();
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    /// 打开采集设备（非阻塞模式 + poll 超时）。
    /// @return 成功 true；失败填 error
    bool Open(const CaptureConfig& cfg, std::string& error);

    /// 读当前可用数据（上限 max_samples，调用方按 frame_samples 切帧）。
    /// 返回值：>0 = 样本数；0 = 超时/无数据/静音期丢帧（调用方查退出标志）；-1 = 错误
    int ReadFrame(int16_t* out, int max_samples, int timeout_ms);

    /// 播报期闭麦：丢帧但保持流排空（不 stop/reopen，避免数字静音坑复现）
    void Mute(bool on) { muted_.store(on); }
    bool muted() const { return muted_.load(); }

    /// Close 前必须已 join 采集线程（本层不做跨线程同步）
    void Close();
    bool IsOpen() const { return pcm_ != nullptr; }

private:
    snd_pcm_t* pcm_ = nullptr;
    int frame_samples_ = 480;
    std::atomic<bool> muted_{false};
};

class AudioPlayback {
public:
    AudioPlayback() = default;
    ~AudioPlayback();
    AudioPlayback(const AudioPlayback&) = delete;
    AudioPlayback& operator=(const AudioPlayback&) = delete;

    bool Open(const PlaybackConfig& cfg, std::string& error);

    /// 阻塞写完（分块检查 Abort）；Abort 后返回 false
    bool Write(const int16_t* data, size_t samples);

    /// 等到全部播完（播报结束判定）；Abort 后立即返回 false
    bool Drain();

    /// 请求中断当前/后续写入（退出路径用；线程安全，不跨线程碰 ALSA 句柄）
    void Abort() { abort_.store(true); }

    void Close();
    bool IsOpen() const { return pcm_ != nullptr; }

private:
    snd_pcm_t* pcm_ = nullptr;
    int sample_rate_ = 48000;
    std::atomic<bool> abort_{false};
};

}  // namespace prosophor::nr::infra
