// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// VoiceInteraction：ASR + TTS + 音频 I/O 的运行时装配。
//
// 两种应用形态共用：
//   - nutribot：use_alsa_audio=true，Init 内部创建 ALSA 输入/输出 + EnergyVad，
//     Init 期创建并预热 provider，内部起采集线程，播报期硬件闭麦；
//   - virtual_sprite：注入 MediaEngineAudioOutput，default_webrtc_vad=true，
//     Init 期按配置创建 provider（无预热），采集由 media_engine::VoiceChannel 驱动。
//
// 线程/退出顺序：StopCapture（mute -> join 采集 -> close input）
//   -> StopAudio（output.Abort -> pipeline.Stop -> asr.reset -> tts.reset -> close output）
// provider 的 ensure/调用分别由 asr_mutex_/tts_mutex_ 串行化；播放不持 tts_mutex_。

#include <atomic>
#include <cstddef>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/noncopyable.h"
#include "audio/audio_io.h"
#include "tts_audio_cache.h"
#include "vad/vad.h"
#include "voice_interaction/voice_config.h"
#include "voice_interaction/voice_pipeline.h"
#include "voice_interaction/providers/asr/asr_provider.h"
#include "voice_interaction/providers/tts/tts_provider.h"

namespace prosophor::voice {

struct VoiceInteractionConfig {
    AsrConfig asr;
    TtsConfig tts;
    VoiceConfig voice;
    VoiceTuning tuning;

    bool warmup_providers = true;      // Init 时预热（nutribot）
    bool require_providers = true;     // provider 不可用则 Init 失败（nutribot）
    bool sanitize_tts_text = true;     // 合成前做 SanitizeTtsText
    bool default_webrtc_vad = false;   // vad_backend=auto 时的形态默认（SDL=true）
    bool use_alsa_audio = false;       // true = 按 voice 设备名内部创建 ALSA 输入/输出
    size_t tts_cache_bytes = 0;        // 0 = 关闭 TTS 缓存
};

class VoiceInteraction : public Noncopyable {
public:
    VoiceInteraction();
    ~VoiceInteraction();

    /// 使用形态默认依赖：VAD/音频输入/输出由 config 内部创建。
    /// 适用 nutribot（ALSA）等完全按配置装配的形态。
    bool Init(const VoiceInteractionConfig& config, std::string& error);

    /// 注入 VAD 与音频适配器；audio_input 非空时内部起采集线程。
    /// 注入的端口优先于 config 内部创建（SDL 注入 MediaEngineAudioOutput）。
    /// 失败时已释放已获取的资源，Stop() 仍安全。
    bool Init(const VoiceInteractionConfig& config,
              std::unique_ptr<IVad> vad,
              std::unique_ptr<AudioInput> audio_input,
              std::unique_ptr<AudioOutput> audio_output,
              std::string& error);

    /// 启动 VoicePipeline workers 与采集线程。只能在 Init 成功后调用一次。
    void Start();

    // ── 主循环接口 ──
    /// 无结果时 out 清空并返回 false
    bool PollAsrResult(std::string& out, int timeout_ms = 0);
    bool PollAsrLatest(std::string& out);
    /// 取走当前所有 final（非阻塞）；返回条数，out 清空后按入队顺序填入
    size_t PollAllAsrResults(std::vector<std::string>& out);
    /// 用户是否还在说（VAD 正在累积当前句）
    bool IsSpeechActive() const;
    /// 是否还有语音待处理（识别中，或 final 尚未被主循环取走）
    bool HasPendingAsr() const;
    /// 入队播报；返回是否真正入队（TTS 关闭/文本为空/队列已关闭时为 false）
    bool Speak(SpeechRequest request);
    bool Speak(std::string text) {
        return Speak(SpeechRequest{std::move(text), {}, {}});
    }
    /// 预合成并写入 TTS 缓存（不播放）；用于唤醒应答等固定短语
    bool WarmSpeech(const std::string& text);
    /// 正在播报（已入队未开播也算，见 VoicePipeline::IsSpeaking）
    bool IsSpeaking() const;
    /// 丢弃尚未开播的播报（如思考填充语）；返回丢弃条数
    size_t DropPendingSpeech();
    /// 手动闭麦/开麦（思考期闭麦）；开麦时顺带重置 VAD。
    /// 闭麦期间 TTS worker 播完单句不再自行开麦（多句连播不留听音缝隙）。
    void SetMicMuted(bool muted);
    /// 轮内等待后续句子（思考填充语 → LLM 首句）：播完最后一句后不立刻停流，
    /// 用静音垫着，避免停流/重启的"开关"声。首句入队后由调用方清除。
    void SetPlaybackHold(bool hold);
    VoicePipeline::Stats stats() const;

    // ── 采集线程 / SDL 回调调用 ──
    void FeedFrame(const int16_t* pcm, size_t samples);
    void ResetUtterance();

    // ── 拉取式双工传输（SIP/设备 send 回调）──
    /// 传输层发送回调：拉一帧播放 PCM；返回是否有真实音频（false = 队列空）
    bool PullPlaybackFrame(int16_t* out, size_t samples);
    /// 尚未被传输层取走的播放样本数
    size_t QueuedPlaybackSamples() const;

    // ── 退出（两阶段，顺序敏感）──
    void StopCapture();   // mute -> join 采集线程 -> input.Close
    void StopAudio();     // output.Abort -> pipeline.Stop -> asr/tts reset -> output.Close
    void Stop();          // StopCapture + StopAudio；幂等

private:
    AsrOptions BuildAsrOptions() const;
    AsrResponse Transcribe(const std::vector<int16_t>& pcm);

    /// 文本 → PCM（同步、带 TTS 缓存）；仅 SpeakBlocking 调用
    TtsResponse Synthesize(const SpeechRequest& request);
    /// TTS worker 专用：合成 + 播放 + 播报期闭麦
    bool SpeakBlocking(const SpeechRequest& request);
    /// 队列空但轮次未结束：写静音维持 PCM 流，直到新句子入队或 hold 清除
    void BridgeSilence(int sample_rate, int channels);

    void CaptureLoop();
    void NormalizeRequest(SpeechRequest& request) const;

    VoiceInteractionConfig config_;
    TtsAudioCache cache_;

    // 成员顺序固定：析构时 pipeline_ 先停，最后 provider 销毁
    std::unique_ptr<AsrProvider> asr_;
    std::unique_ptr<TtsProvider> tts_;
    std::unique_ptr<AudioOutput> audio_out_;
    std::unique_ptr<AudioInput> audio_in_;
    std::unique_ptr<VoicePipeline> pipeline_;

    mutable std::mutex asr_mutex_;
    mutable std::mutex tts_mutex_;
    /// 外部（话轮侧）要求持续闭麦：worker 播完一句不得自行开麦
    std::atomic<bool> mic_held_muted_{false};
    /// 轮内等待后续句子：播完当前句后不断流，用静音维持
    std::atomic<bool> playback_hold_{false};

    std::thread capture_thread_;
    std::atomic<bool> capture_running_{false};
    bool started_ = false;
};

}  // namespace prosophor::voice
