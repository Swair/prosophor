// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "virtual_sprite/media_engine_audio_output.h"

#include <vector>

#include "media/audio/voice_channel.h"
#include "voice_interaction/audio/resampler.h"

namespace prosophor::voice {

bool MediaEngineAudioOutput::Open(int sample_rate, int channels, std::string& error) {
    if (sample_rate <= 0 || channels <= 0) {
        error = "invalid TTS sample rate/channels";
        return false;
    }
    sample_rate_ = sample_rate;
    channels_ = channels;
    return true;
}

bool MediaEngineAudioOutput::Write(const int16_t* data, size_t samples) {
    if (data == nullptr || samples == 0) {
        return false;
    }
    std::vector<int16_t> pcm(data, data + samples);
    if (sample_rate_ != 16000 || channels_ != 1) {
        pcm = ResampleTo16k(pcm, sample_rate_, channels_);
    }
    if (pcm.empty()) {
        return false;
    }
    media_engine::VoiceChannel::GetInstance().PlayAudio(pcm);
    return true;
}

}  // namespace prosophor::voice
