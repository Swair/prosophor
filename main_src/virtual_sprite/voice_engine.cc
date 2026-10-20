// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "virtual_sprite/voice_engine.h"

#include "config/config.h"
#include "common/log_wrapper.h"
#include "media/audio/voice_channel.h"
#include "virtual_sprite/media_engine_audio_output.h"
#include "voice_interaction/voice_interaction.h"

namespace prosophor {

VoiceEngine& VoiceEngine::GetInstance() {
    static VoiceEngine instance;
    return instance;
}

VoiceEngine::VoiceEngine() {
    const auto& config = ProsophorConfig::GetInstance();

    voice::VoiceInteractionConfig interaction_config;
    interaction_config.asr = config.asr;
    interaction_config.tts = config.tts;
    interaction_config.voice = config.voice;

    // 保留虚精灵旧状态机参数：WebRTC VAD、20 帧收句、无最短句/峰值闸、
    // 边说边出 interim、final 不因上一句在飞而丢弃
    interaction_config.tuning.frame_samples = 480;
    interaction_config.tuning.start_confirm_frames = 3;
    interaction_config.tuning.end_silence_frames = 20;
    interaction_config.tuning.max_utterance_frames = 0;
    interaction_config.tuning.pre_roll_frames = 5;
    interaction_config.tuning.min_utterance_frames = 0;
    interaction_config.tuning.min_peak = 0;
    interaction_config.tuning.drop_utterance_if_asr_busy = false;
    interaction_config.tuning.emit_interim = true;
    interaction_config.tuning.interim_voiced_frames = 10;
    interaction_config.tuning.interim_silence_flush_frames = 3;
    interaction_config.tuning.interim_max_silence_frames = 3;

    interaction_config.warmup_providers = false;   // 桌面端不做模型预热
    interaction_config.require_providers = false;
    interaction_config.sanitize_tts_text = true;
    interaction_config.default_webrtc_vad = true;   // auto -> WebRTC（桌面默认）
    interaction_config.tts_cache_bytes = config.voice.tts_cache_bytes;

    interaction_ = std::make_unique<voice::VoiceInteraction>();
    std::string error;
    if (!interaction_->Init(interaction_config, /*vad=*/nullptr,
                            /*audio_input=*/nullptr,
                            std::make_unique<voice::MediaEngineAudioOutput>(),
                            error)) {
        LOG_ERROR("[VoiceEngine] 初始化失败: {}", error.c_str());
        interaction_.reset();
        return;
    }
    interaction_->Start();
}

VoiceEngine::~VoiceEngine() {
    if (interaction_) {
        interaction_->Stop();
    }
    media_engine::VoiceChannel::GetInstance().Stop();
}

std::string VoiceEngine::PollAsrLatest() {
    if (interaction_ == nullptr) {
        return {};
    }
    auto result = interaction_->PollAsrLatest();
    return result.has_value() ? std::move(*result) : std::string{};
}

void VoiceEngine::StartCapture() {
    if (interaction_) {
        interaction_->ResetUtterance();
    }
    media_engine::VoiceChannel::GetInstance().InitCapture(
        [this](const int16_t* data, size_t n) {
            if (n != 480 || interaction_ == nullptr) {
                return;
            }
            // VoiceChannel 没有硬件闭麦；播报期直接丢帧，避免自听
            if (!interaction_->IsSpeaking()) {
                interaction_->FeedFrame(data, n);
            }
        });
}

void VoiceEngine::StopCapture() {
    media_engine::VoiceChannel::GetInstance().EnableCapture(false);
}

void VoiceEngine::Speak(const std::string& text,
                         const std::string& backend,
                         const std::string& voice) {
    if (voice == "none" || interaction_ == nullptr) {
        return;  // skip TTS for roles that don't need it
    }
    interaction_->Speak(voice::SpeechRequest{text, voice, backend});
}

}  // namespace prosophor
