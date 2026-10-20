// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

// VoiceInteraction 装配与生命周期见 voice_interaction.h。
#include "voice_interaction/voice_interaction.h"

#include <algorithm>
#include <thread>
#include <utility>

#include "common/log_wrapper.h"
#include "common/time_wrapper.h"
#include "providers/asr_tts_provider_factory.h"
#include "speech_text.h"
#include "vad/vad_factory.h"
#ifdef PROSOPHOR_HAS_ALSA
#include "audio/alsa_audio_io.h"
#endif

namespace prosophor::voice {

VoiceInteraction::VoiceInteraction() = default;

VoiceInteraction::~VoiceInteraction() {
    Stop();
}

bool VoiceInteraction::Init(const VoiceInteractionConfig& config, std::string& error) {
    return Init(config, /*vad=*/nullptr, /*audio_input=*/nullptr,
                /*audio_output=*/nullptr, error);
}

bool VoiceInteraction::Init(const VoiceInteractionConfig& config,
                            std::unique_ptr<IVad> vad,
                            std::unique_ptr<AudioInput> audio_input,
                            std::unique_ptr<AudioOutput> audio_output,
                            std::string& error) {
    config_ = config;
    cache_.SetMaxBytes(config_.tts_cache_bytes);

    // 形态自带的音频后端：按 voice 设备配置内部创建；注入的端口优先
#ifdef PROSOPHOR_HAS_ALSA
    if (config_.use_alsa_audio) {
        if (!audio_input) {
            CaptureConfig capture_cfg;
            capture_cfg.device = config_.voice.capture_device;
            audio_input = std::make_unique<AlsaAudioInput>(capture_cfg);
        }
        if (!audio_output) {
            PlaybackConfig playback_cfg;
            playback_cfg.device = config_.voice.playback_device;
            audio_output = std::make_unique<AlsaAudioOutput>(playback_cfg);
        }
    }
#else
    if (config_.use_alsa_audio) {
        error = "ALSA audio backend not built";
        return false;
    }
#endif

    audio_in_ = std::move(audio_input);
    audio_out_ = std::move(audio_output);

    if (audio_in_) {
        std::string open_error;
        if (!audio_in_->Open(open_error)) {
            error = open_error;
            audio_in_.reset();
            audio_out_.reset();
            return false;
        }
    }

    // 按配置直接创建 provider；是否容忍创建失败由 require_providers 决定
    asr_ = AsrProviderFactory::CreateProvider(config_.asr);
    tts_ = TtsProviderFactory::CreateProvider(config_.tts);

    if (config_.warmup_providers) {
        if (asr_) {
            std::string warm_error;
            if (!asr_->WarmUp(warm_error)) {   // 预创建（约 1.6s），挪出首句
                LOG_ERROR("VoiceInteraction: ASR 预热失败: {}", warm_error.c_str());
            }
        }
        if (tts_) {
            std::string warm_error;
            if (!tts_->WarmUp(warm_error)) {   // 预创建（约 2.4s），挪出首播
                LOG_ERROR("VoiceInteraction: TTS 预热失败: {}", warm_error.c_str());
            }
        }
    }

    if (config_.require_providers && (asr_ == nullptr || tts_ == nullptr)) {
        error = "语音后端不可用（asr=" + std::string(asr_ ? "ok" : "null") +
                " tts=" + std::string(tts_ ? "ok" : "null") +
                "）—— 检查 settings.json 的 asr/tts 段（backend=aidvoice 与模型路径）";
        Stop();
        return false;
    }

    if (!vad) {
        vad = CreateVad(config_.voice, config_.default_webrtc_vad);
    }

    // start_workers=false：等 pipeline_ 赋值完成后再 Start()，回调里不会碰到空指针
    pipeline_ = std::make_unique<VoicePipeline>(
        [this](const std::vector<int16_t>& pcm) {
            const AsrResponse response = Transcribe(pcm);
            return response.success ? response.text : std::string{};
        },
        [this](const SpeechRequest& request) { return SpeakBlocking(request); },
        config_.tuning, /*start_workers=*/false, std::move(vad));

    return true;
}

void VoiceInteraction::Start() {
    if (started_ || !pipeline_) {
        return;
    }
    started_ = true;
    pipeline_->Start();
    if (audio_in_) {
        capture_running_.store(true);
        capture_thread_ = std::thread([this] { CaptureLoop(); });
    }
}

std::optional<std::string> VoiceInteraction::PollAsrResult(int timeout_ms) {
    return pipeline_ ? pipeline_->PollAsrResult(timeout_ms) : std::nullopt;
}

std::optional<std::string> VoiceInteraction::PollAsrLatest() {
    return pipeline_ ? pipeline_->PollAsrLatest() : std::nullopt;
}

// TTS 三个入口各司其职：
//   Speak()         —— 入队，非阻塞（主循环/主动播报）
//   Synthesize()    —— 文本→PCM（同步、带缓存；worker 内部用）
//   SpeakBlocking() —— worker 专用：合成 + 播放 + 闭麦
void VoiceInteraction::Speak(SpeechRequest request) {
    if (request.text.empty() || request.voice == "none" || !config_.tts.enabled) {
        return;
    }
    // 净化/默认 voice/backend 统一在 Synthesize() 里做一次
    if (pipeline_) {
        pipeline_->EnqueueSpeech(std::move(request));
    }
}

bool VoiceInteraction::IsSpeaking() const {
    if (pipeline_ != nullptr && pipeline_->IsSpeaking()) {
        return true;
    }
    // 拉取式输出：TTS worker 已写完但传输层还没拉完，仍算播报中
    return audio_out_ != nullptr && audio_out_->QueuedSamples() > 0;
}

VoicePipeline::Stats VoiceInteraction::stats() const {
    return pipeline_ ? pipeline_->stats() : VoicePipeline::Stats{};
}

void VoiceInteraction::FeedFrame(const int16_t* pcm, size_t samples) {
    if (pipeline_) {
        pipeline_->FeedFrame(pcm, samples);
    }
}

void VoiceInteraction::ResetUtterance() {
    if (pipeline_) {
        pipeline_->ResetUtterance();
    }
}

bool VoiceInteraction::PullPlaybackFrame(int16_t* out, size_t samples) {
    return audio_out_ != nullptr && audio_out_->PullFrame(out, samples);
}

size_t VoiceInteraction::QueuedPlaybackSamples() const {
    return audio_out_ != nullptr ? audio_out_->QueuedSamples() : 0;
}

void VoiceInteraction::NormalizeRequest(SpeechRequest& request) const {
    if (request.voice.empty()) {
        request.voice = config_.tts.voice;
    }
    if (request.backend.empty()) {
        request.backend = config_.tts.backend;
    }
    if (config_.sanitize_tts_text) {
        request.text = SanitizeTtsText(NormalizeTtsWhitespace(request.text));
    }
}

AsrOptions VoiceInteraction::BuildAsrOptions() const {
    AsrOptions options;
    options.language = config_.asr.language;
    options.prompt = config_.asr.prompt;
    options.interim_results = config_.asr.interim_results;
    options.punctuation = config_.asr.punctuation;
    options.inverse_text_normalization = config_.asr.inverse_text_normalization;
    options.max_sentence_silence_ms = config_.asr.max_sentence_silence_ms;
    options.customization_id = config_.asr.customization_id;
    options.vocabulary_id = config_.asr.vocabulary_id;
    return options;
}

AsrResponse VoiceInteraction::Transcribe(const std::vector<int16_t>& pcm) {
    std::lock_guard<std::mutex> lock(asr_mutex_);
    if (!asr_) {
        AsrResponse response;
        response.error_msg = "ASR provider unavailable";
        return response;
    }
    const AsrRequest request{pcm.data(), pcm.size(), BuildAsrOptions()};
    const AsrResponse response = asr_->AsrProcess(request);
    if (!response.success && !response.error_msg.empty()) {
        LOG_ERROR("[VoiceInteraction] ASR: {}", response.error_msg.c_str());
    }
    return response;
}

TtsResponse VoiceInteraction::Synthesize(const SpeechRequest& request) {
    SpeechRequest normalized = request;
    NormalizeRequest(normalized);
    if (normalized.text.empty()) {
        return {false, "empty text after sanitization", {}, 0, 0};
    }

    std::lock_guard<std::mutex> lock(tts_mutex_);
    if (!tts_) {
        return {false, "no provider", {}, 0, 0};
    }

    const TtsRequest provider_request{normalized.text, normalized.voice};
    const TtsCacheKey key{provider_request.text, provider_request.voice,
                          normalized.backend, provider_request.sample_rate,
                          provider_request.channels};
    TtsResponse cached;
    if (cache_.Get(key, cached)) {
        return cached;
    }

    TtsResponse result = tts_->Synthesize(provider_request);
    if (result.success && !result.pcm.empty()) {
        cache_.Put(key, result);
    }
    return result;
}

bool VoiceInteraction::SpeakBlocking(const SpeechRequest& request) {
    // 播报期闭麦 + 前后重置 VAD：防自听（v1 不做 barge-in）
    if (audio_in_) {
        audio_in_->Mute(true);
    }

    const TtsResponse response = Synthesize(request);
    bool ok = false;
    if (response.success && !response.pcm.empty()) {
        if (audio_out_ == nullptr) {
            LOG_ERROR("[VoiceInteraction] 无播放输出适配器");
        } else {
            std::string error;
            if (!audio_out_->EnsureOpen(response.sample_rate, response.channels, error)) {
                LOG_ERROR("[VoiceInteraction] {}", error.c_str());
            } else {
                ok = audio_out_->Write(response.pcm.data(), response.pcm.size()) &&
                     audio_out_->Drain();
            }
        }
    } else {
        LOG_ERROR("[VoiceInteraction] TTS 合成失败: {}", response.error_msg.c_str());
    }

    if (audio_in_) {
        audio_in_->Mute(false);
    }
    if (pipeline_) {
        pipeline_->ResetUtterance();
    }
    return ok;
}

void VoiceInteraction::CaptureLoop() {
    const size_t frame = static_cast<size_t>(
        std::max(1, config_.tuning.frame_samples));
    std::vector<int16_t> buffer(frame * 10);   // 一次最多取 300ms（抗调度抖动）
    std::vector<int16_t> pending;              // 不足一帧的余数，跨读续接
    int discarded_frames = 0;

    while (capture_running_.load()) {
        const int got = audio_in_->Read(buffer.data(), static_cast<int>(buffer.size()), 100);
        if (got < 0) {
            std::this_thread::sleep_for(prosophor::Milliseconds(10));  // 错误时不空转
            continue;
        }
        if (got == 0) {
            continue;
        }
        pending.insert(pending.end(), buffer.begin(), buffer.begin() + got);
        size_t offset = 0;
        while (offset + frame <= pending.size()) {
            if (discarded_frames < config_.voice.warmup_discard_frames) {
                ++discarded_frames;   // 设备唤醒期可能是数字静音（alsa_audio_io.h 已知坑）
            } else if (pipeline_) {
                pipeline_->FeedFrame(pending.data() + offset, frame);
            }
            offset += frame;
        }
        pending.erase(pending.begin(),
                      pending.begin() + static_cast<std::ptrdiff_t>(offset));
    }
}

void VoiceInteraction::StopCapture() {
    if (audio_in_) {
        audio_in_->Mute(true);
    }
    capture_running_.store(false);
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    if (audio_in_) {
        audio_in_->Close();
    }
}

void VoiceInteraction::StopAudio() {
    if (audio_out_) {
        audio_out_->Abort();   // 让在飞的播放立即返回
    }
    if (pipeline_) {
        pipeline_->Stop();      // join ASR/TTS worker：此后不再有 SDK 调用
    }
    {
        std::lock_guard<std::mutex> lock(asr_mutex_);
        asr_.reset();          // 析构 -> SDK destroy（_Exit 前必须显式调用）
    }
    {
        std::lock_guard<std::mutex> lock(tts_mutex_);
        tts_.reset();
    }
    if (audio_out_) {
        audio_out_->Close();
    }
}

void VoiceInteraction::Stop() {
    StopCapture();
    StopAudio();
}

}  // namespace prosophor::voice
