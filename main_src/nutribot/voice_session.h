// voice_session：常驻语音会话（能量 VAD 切句 + ASR/TTS 工作线程）
//
// 数据流：
//   采集线程 FeedFrame(30ms) → VAD 状态机 → utterance 成形 → ASR worker → 文本队列
//   主循环 PollUtterance() 取文本派发；Speak(text) 入 TTS 队列 → TTS worker 合成+播放
//
// 设计要点：
//   - 纯逻辑 + 线程骨架，不碰音频设备与 SDK：ASR/TTS 以 std::function 注入
//     （仿 vl_food_detector 的 VisionQueryBackend 注入范式），单测注 Fake
//   - 三道闸防误触：最短句长、峰值自检（数字静音坑，见 audio_io.h）、单飞
//     （上一句还在 ASR 中就丢弃新句）
//   - TTS 队列深度 1 丢旧保新：宁可漏报，不播"5 分钟前的提醒"
//   - 播报期闭麦由调用方（采集线程用 AudioCapture::Mute）负责；播报前后
//     调 ResetUtterance() —— 置标志、由采集线程在下一帧生效（免锁）
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace prosophor::nr::app {

// ASR 后端：一句 utterance PCM（16k 单声道 int16）→ 文本；空串 = 失败/未听懂
using TranscribeFn = std::function<std::string(const std::vector<int16_t>&)>;
// 播报后端：阻塞至播完；false = 失败/被中断
using SpeakFn = std::function<bool(const std::string&)>;

struct VoiceTuning {
    int frame_samples = 480;          // 帧长（样本；30ms @16k，与 ALSA 帧一致）
    double vad_rms_dbfs = -38.0;      // 能量门限（dBFS）
    int start_confirm_frames = 3;     // 起判确认帧数（90ms）
    int end_silence_frames = 23;      // 句尾静音帧数（约 700ms）
    int max_utterance_frames = 400;   // 单句上限（约 12s；须 < asr.echo_ms）
    int pre_roll_frames = 5;          // 起始回捞帧数（150ms，防首字被切）
    int min_utterance_frames = 10;    // 最短句（约 300ms，防咳嗽/碰桌误触）
    int min_peak = 16;                // 峰值闸：低于此值视为数字静音丢弃
    size_t max_pending_speech = 1;    // TTS 队列深度（丢旧保新）
};

class VoiceSession {
public:
    VoiceSession(TranscribeFn transcribe, SpeakFn speak, VoiceTuning tuning,
                 bool start_workers = true);
    ~VoiceSession();
    VoiceSession(const VoiceSession&) = delete;
    VoiceSession& operator=(const VoiceSession&) = delete;

    void Start();   // start_workers=false 时由调用方显式启动
    /// 停止并 join 两个 worker；幂等（退出路径必须在 SDK destroy 前调用）
    void Stop();

    // ── 采集线程调用 ──

    /// 送入一帧 PCM（不足一帧的部分忽略；由采集层保证整帧）
    void FeedFrame(const int16_t* pcm, size_t samples);
    /// 请求清空 VAD 状态（播报前后调用；置标志，下一帧生效）
    void ResetUtterance() { reset_requested_.store(true); }

    // ── 主循环调用 ──

    /// 取一句已识别的文本；timeout_ms=0 为非阻塞
    std::optional<std::string> PollUtterance(int timeout_ms = 0);
    /// 入队播报（深度 1 丢旧保新；空文本忽略）
    void Speak(std::string text);
    /// 是否正在播报（主循环据此跳过会话轮/感知派发）
    bool speaking() const { return speaking_.load(); }

    struct Stats {
        int feed_frames = 0;       // 送入帧数
        int utterances = 0;        // 提交给 ASR 的句数
        int dropped_short = 0;     // 短于 min_utterance_frames
        int dropped_lowpeak = 0;   // 峰值低于 min_peak（数字静音）
        int dropped_busy = 0;      // 上一句还在 ASR 中
        int asr_empty = 0;         // ASR 返回空串
        int spoken = 0;            // 成功播报次数
        int speak_failed = 0;      // 播报失败
        int speak_dropped = 0;     // TTS 队列丢旧计数
    };
    Stats stats() const;

private:
    void AsrWorker();
    void TtsWorker();
    void ResetVadState();   // 仅采集线程调用

    TranscribeFn transcribe_;
    SpeakFn speak_;
    VoiceTuning tuning_;

    // ── VAD 状态（仅采集线程读写）──
    bool in_speech_ = false;
    int start_confirm_ = 0;
    int silence_frames_ = 0;
    std::vector<int16_t> utterance_;                       // 当前句累积
    std::deque<std::vector<int16_t>> pre_roll_;            // 起始回捞环形
    std::atomic<bool> reset_requested_{false};

    // ── ASR 队列（采集线程 → ASR worker）──
    std::mutex asr_mutex_;
    std::condition_variable asr_cv_;
    std::deque<std::vector<int16_t>> asr_queue_;
    std::atomic<int> asr_in_flight_{0};   // 队列 + 正在识别（单飞判定）

    // ── 文本队列（ASR worker → 主循环）──
    std::mutex text_mutex_;
    std::condition_variable text_cv_;
    std::deque<std::string> text_queue_;

    // ── TTS 槽（主循环 → TTS worker；深度 1）──
    std::mutex tts_mutex_;
    std::condition_variable tts_cv_;
    std::string pending_speech_;
    bool has_pending_speech_ = false;
    std::atomic<bool> speaking_{false};

    std::atomic<bool> running_{false};
    std::thread asr_thread_;
    std::thread tts_thread_;

    // ── 统计（各线程写，快照读）──
    mutable std::mutex stats_mutex_;
    Stats stats_;
    void BumpCounter(int Stats::*field);
};

/// 播报文本净化（纯函数，可单测）：去 URL/markdown/emoji、压空白、按
/// 句末标点截断到 max_chars（UTF-8 码点计）
std::string SanitizeForSpeech(const std::string& reply, size_t max_chars);

/// LLM 的"不接话"约定输出（nutribot 角色 system prompt 教它：桌边闲聊/与己无关
/// 时只输出这个标记）。应用侧识别到 -> 只进对话上下文，不开口。
constexpr const char* kSilenceMarker = "（沉默）";

/// 是否为沉默应答（纯函数）：去空白与两端标点后与标记相同（兼容半角括号写法）
bool IsSilentReply(const std::string& reply);
/// 剥掉嵌在正文里的沉默标记（"（沉默）好的呀" -> "好的呀"）；无标记原样返回
std::string StripSilenceMarker(const std::string& text);

}  // namespace prosophor::nr::app
