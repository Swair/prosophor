// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "resampler.h"
#include <cstddef>

namespace prosophor {

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

}  // namespace prosophor
