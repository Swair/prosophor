// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "audio/resampler.h"

#include <algorithm>
#include <cstddef>

namespace prosophor::voice {

std::vector<int16_t> Resample24kTo16k(const std::vector<int16_t>& input) {
    // 24k → 16k = 2 output for every 3 input
    size_t out_count = input.size() * 2 / 3;
    std::vector<int16_t> output(out_count);
    for (size_t i = 0; i < out_count; ++i) {
        size_t src = i * 3 / 2;
        if (src + 1 < input.size()) {
            int32_t a = input[src];
            int32_t b = input[src + 1];
            int32_t frac = (i * 3) % 2;
            output[i] = static_cast<int16_t>((a * (2 - frac) + b * frac) / 2);
        } else {
            output[i] = input[src];
        }
    }
    return output;
}

std::vector<int16_t> ResampleTo16k(const std::vector<int16_t>& input,
                                   int source_rate, int source_channels) {
    if (input.empty()) {
        return {};
    }
    const int channels = source_channels > 0 ? source_channels : 1;
    if (channels == 1 && source_rate == 24000) {
        return Resample24kTo16k(input);
    }

    // 先下混为单声道
    std::vector<int16_t> mono;
    if (channels == 1) {
        mono = input;
    } else {
        const size_t frames = input.size() / static_cast<size_t>(channels);
        mono.resize(frames);
        for (size_t i = 0; i < frames; ++i) {
            int32_t sum = 0;
            for (int c = 0; c < channels; ++c) {
                sum += input[i * static_cast<size_t>(channels) + static_cast<size_t>(c)];
            }
            mono[i] = static_cast<int16_t>(sum / channels);
        }
    }

    if (source_rate <= 0 || source_rate == 16000) {
        return mono;
    }

    const size_t out_count =
        static_cast<size_t>(static_cast<double>(mono.size()) * 16000.0 / source_rate);
    std::vector<int16_t> output(out_count);
    for (size_t i = 0; i < out_count; ++i) {
        const double src_pos = static_cast<double>(i) * source_rate / 16000.0;
        const size_t i0 = static_cast<size_t>(src_pos);
        const size_t i1 = std::min(i0 + 1, mono.size() - 1);
        const double frac = src_pos - static_cast<double>(i0);
        output[i] = static_cast<int16_t>(
            static_cast<double>(mono[i0]) * (1.0 - frac) +
            static_cast<double>(mono[i1]) * frac);
    }
    return output;
}

}  // namespace prosophor::voice
