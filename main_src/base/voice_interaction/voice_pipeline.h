// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// voice_pipeline：语音交互核心流水线（VAD 切句 + ASR/TTS 工作线程）
//
// 数据流：
//   采集线程 FeedFrame(30ms) → VAD 状态机 → utterance 成形 → ASR worker → 文本队列
//   主循环 PollAsrResult() 取文本派发；EnqueueSpeech(text) 入 TTS 队列 → TTS worker 合成+播放
//
// 设计要点：
//   - 纯逻辑 + 线程骨架，不碰音频设备与 SDK：ASR/TTS 以 std::function 注入，
//     VAD 以 IVad 注入（nutribot=EnergyVad，SDL=WebRTC VAD 适配器），单测注 Fake
//   - 三道闸防误触：最短句长、峰值自检（数字静音坑）、单飞（上一句还在 ASR 中就丢弃）
//   - TTS 队列有界（默认 10）丢旧保新：宁可漏报，不播"5 分钟前的提醒"
//   - SDL 需要边说边出 interim：emit_interim=true，interim 只进 PollAsrLatest，
//     不进 PollAsrResult（final-only）
//   - 播报期闭麦由调用方（采集线程用 AudioInput::Mute / SDL 丢帧）负责；
//     播报前后调 ResetUtterance() —— 置标志、由采集线程在下一帧生效（免锁）

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "common/blocking_queue.h"
#include "vad/vad.h"
#include "voice_interaction/voice_config.h"

namespace prosophor::voice {

// ASR 后端：一句 utterance PCM（16k 单声道 int16）→ 文本；空串 = 失败/未听懂
using TranscribeFn = std::function<std::string(const std::vector<int16_t>&)>;

/// 播报请求：text 必填；voice/backend 为空 = 使用配置默认值
struct SpeechRequest {
    std::string text;
    std::string voice;
    std::string backend;
};

// 播报后端：阻塞至播完；false = 失败/被中断
using SpeakFn = std::function<bool(const SpeechRequest&)>;

struct VoiceTuning {
    int frame_samples = 480;          // 帧长（样本；30ms @16k，与 ALSA 帧一致）
    double vad_rms_dbfs = -38.0;      // 能量门限（dBFS，默认 EnergyVad 使用）
    int start_confirm_frames = 3;     // 起判确认帧数（90ms）
    int end_silence_frames = 23;      // 句尾静音帧数（约 700ms）
    int max_utterance_frames = 400;   // 单句上限（约 12s；0 = 不限长）
    int pre_roll_frames = 5;          // 起始回捞帧数（150ms，防首字被切）
    int min_utterance_frames = 10;    // 最短句（约 300ms；0 = 关闭该闸）
    int min_peak = 16;                // 峰值闸：低于此值视为数字静音（0 = 关闭）
    size_t max_pending_speech = 10;   // TTS 待播队列上限（1 = 只保留最新）
    size_t max_pending_texts = 4;     // final 文本队列上限（防主循环卡住堆积）
    size_t max_pending_asr_jobs = 8;  // ASR 队列上限（满时拒绝新句，保留旧句）

    bool drop_utterance_if_asr_busy = true;  // 上一句在飞时丢弃新句（nutribot）
    bool emit_interim = false;               // SDL：边说边出 interim
    int interim_voiced_frames = 10;          // 有声累计帧数触发 interim
    int interim_silence_flush_frames = 3;    // 静音初段 flush 前的最少间隔
    int interim_max_silence_frames = 3;      // 静音超过此帧数不再 flush
};

/// VoiceConfig → VoiceTuning（帧长固定 30ms @16k）
VoiceTuning BuildVoiceTuning(const VoiceConfig& cfg);

class VoicePipeline {
public:
    VoicePipeline(TranscribeFn transcribe, SpeakFn speak, VoiceTuning tuning,
                 bool start_workers = true, std::unique_ptr<IVad> vad = nullptr);
    ~VoicePipeline();
    VoicePipeline(const VoicePipeline&) = delete;
    VoicePipeline& operator=(const VoicePipeline&) = delete;

    void Start();   // start_workers=false 时由调用方显式启动
    /// 停止并 join 两个 worker；幂等（退出路径必须在 SDK destroy 前调用）
    void Stop();

    // ── 采集线程调用 ──

    /// 送入一帧 PCM（不足一帧的部分忽略；由采集层保证整帧）
    void FeedFrame(const int16_t* pcm, size_t samples);
    /// 请求清空 VAD 状态（播报前后调用；置标志，下一帧生效）
    void ResetUtterance() { reset_requested_.store(true); }

    // ── 主循环调用 ──

    /// 取一句已识别的文本（仅 final）；timeout_ms=0 为非阻塞
    std::optional<std::string> PollAsrResult(int timeout_ms = 0);
    /// 取最新识别结果（interim 或 final）；SDL 输入框实时显示用
    std::optional<std::string> PollAsrLatest();
    /// 入队播报（有界队列，满时丢最旧；空文本忽略）；实际播放在 TTS worker
    void EnqueueSpeech(SpeechRequest request);
    void EnqueueSpeech(std::string text) {
        EnqueueSpeech(SpeechRequest{std::move(text), {}, {}});
    }
    /// 是否正在播报（主循环据此跳过会话轮/感知派发）
    bool IsSpeaking() const { return speaking_.load(); }

    struct Stats {
        int feed_frames = 0;       // 送入帧数
        int utterances = 0;        // 提交给 ASR 的句数
        int dropped_short = 0;     // 短于 min_utterance_frames
        int dropped_lowpeak = 0;   // 峰值低于 min_peak（数字静音）
        int dropped_busy = 0;      // 上一句还在 ASR 中
        int asr_queue_dropped = 0; // ASR 队列满，拒绝新句
        int asr_empty = 0;         // ASR 返回空串
        int interim_requests = 0;  // 提交的 interim 次数
        int interim_results = 0;   // 产生文本的 interim 次数
        int stale_interim_dropped = 0;  // 迟到 interim 未覆盖较新结果
        int spoken = 0;            // 成功播报次数
    };
    Stats stats() const;

private:
    enum class AsrJobKind { kInterim, kFinal };

    struct AsrJob {
        AsrJobKind kind = AsrJobKind::kFinal;
        std::vector<int16_t> pcm;
        uint64_t seq = 0;
    };

    void AsrWorker();
    void TtsWorker();
    void ResetVadState();      // 仅采集线程调用
    void MaybeFlushInterim(bool voiced);
    void EnqueueAsrJob(AsrJobKind kind, std::vector<int16_t> pcm, uint64_t seq);
    void PublishResult(const std::string& text, uint64_t seq, bool is_final);
    void BumpCounter(int Stats::*field);

    TranscribeFn transcribe_fn_;
    SpeakFn speak_fn_;
    VoiceTuning tuning_;
    std::unique_ptr<IVad> vad_;

    // ── VAD 状态（仅采集线程读写；in_speech_ 供主循环只读）──
    std::atomic<bool> in_speech_{false};
    int start_confirm_ = 0;
    int silence_frames_ = 0;
    int interim_flush_counter_ = 0;
    uint64_t current_seq_ = 0;
    uint64_t next_seq_ = 0;
    std::vector<int16_t> utterance_;                       // 当前句累积
    std::deque<std::vector<int16_t>> pre_roll_;            // 起始回捞环形
    std::atomic<bool> reset_requested_{false};

    // ── ASR 队列（采集线程 → ASR worker，有界 FIFO）──
    BlockingQueue<AsrJob> asr_queue_;
    std::atomic<int> asr_in_flight_{0};   // 队列 + 正在识别（单飞判定）

    // ── 最新结果（interim/final 共用，SDL 轮询）──
    std::mutex result_mutex_;
    std::string latest_result_;
    bool has_latest_result_ = false;
    uint64_t last_published_seq_ = 0;

    // ── 文本队列（ASR worker → 主循环，仅 final）──
    BlockingQueue<std::string> text_queue_;

    // ── TTS 队列（主循环 → TTS worker；有界，满时丢最旧）──
    BlockingQueue<SpeechRequest> speech_queue_;
    std::atomic<bool> speaking_{false};

    std::atomic<bool> running_{false};
    std::thread asr_thread_;
    std::thread tts_thread_;

    // ── 统计（各线程写，快照读）──
    mutable std::mutex stats_mutex_;
    Stats stats_;
};

}  // namespace prosophor::voice
