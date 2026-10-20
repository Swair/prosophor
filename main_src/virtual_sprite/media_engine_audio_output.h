// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// SDL 形态的播放适配器：TTS PCM（任意采样率/声道）→ 16k 单声道 →
// media_engine::VoiceChannel::PlayAudio。VoiceChannel 没有中断 API，
// 故 Abort/Close 是空操作。

#include <cstddef>
#include <cstdint>
#include <string>

#include "voice_interaction/audio/audio_io.h"

namespace prosophor::voice {

class MediaEngineAudioOutput final : public AudioOutput {
public:
    bool Open(int sample_rate, int channels, std::string& error) override;
    bool Write(const int16_t* data, size_t samples) override;
    bool Drain() override { return true; }
    void Abort() override {}
    void Close() override {}
    bool IsOpen() const override { return sample_rate_ > 0; }
    int sample_rate() const override { return sample_rate_; }
    int channels() const override { return channels_; }

private:
    int sample_rate_ = 0;
    int channels_ = 1;
};

}  // namespace prosophor::voice
