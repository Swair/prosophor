// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "audio/frame_queue_audio_output.h"

#include <algorithm>

namespace prosophor::voice {

bool FrameQueueAudioOutput::Open(int sample_rate, int channels, std::string& error) {
    if (sample_rate <= 0 || channels <= 0) {
        error = "invalid TTS sample rate/channels";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    sample_rate_ = sample_rate;
    channels_ = channels;
    queue_.clear();
    return true;
}

bool FrameQueueAudioOutput::Write(const int16_t* data, size_t samples) {
    if (data == nullptr || samples == 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (sample_rate_ <= 0) {
        return false;
    }
    queue_.insert(queue_.end(), data, data + samples);
    return true;
}

void FrameQueueAudioOutput::Abort() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
}

void FrameQueueAudioOutput::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    sample_rate_ = 0;
    channels_ = 1;
}

bool FrameQueueAudioOutput::IsOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sample_rate_ > 0;
}

int FrameQueueAudioOutput::sample_rate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sample_rate_;
}

int FrameQueueAudioOutput::channels() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return channels_;
}

bool FrameQueueAudioOutput::PullFrame(int16_t* out, size_t samples) {
    if (out == nullptr || samples == 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t available = std::min(queue_.size(), samples);
    for (size_t i = 0; i < available; ++i) {
        out[i] = queue_.front();
        queue_.pop_front();
    }
    for (size_t i = available; i < samples; ++i) {
        out[i] = 0;   // 欠载补静音（同 sibot 的 sndBuf_ 空帧）
    }
    return available > 0;
}

size_t FrameQueueAudioOutput::QueuedSamples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

}  // namespace prosophor::voice
