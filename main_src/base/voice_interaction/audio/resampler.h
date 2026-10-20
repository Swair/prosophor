// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

namespace prosophor::voice {

/// Resample 24kHz PCM int16 → 16kHz using linear interpolation (3:2 ratio).
/// No external resampler needed for this simple ratio.
std::vector<int16_t> Resample24kTo16k(const std::vector<int16_t>& input);

/// 任意采样率/声道 → 16kHz 单声道（先下混，再线性插值）。
/// source_rate == 24000 且单声道时走专用的 3:2 快路径。
std::vector<int16_t> ResampleTo16k(const std::vector<int16_t>& input,
                                   int source_rate, int source_channels);

}  // namespace prosophor::voice
