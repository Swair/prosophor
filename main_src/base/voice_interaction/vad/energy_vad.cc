// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "vad/energy_vad.h"

#include <cmath>

namespace prosophor::voice {

double FrameRmsDbfs(const int16_t* pcm, size_t samples) {
    if (samples == 0) {
        return -1000.0;
    }
    double sum = 0.0;
    for (size_t i = 0; i < samples; ++i) {
        const double v = static_cast<double>(pcm[i]);
        sum += v * v;
    }
    const double rms = std::sqrt(sum / static_cast<double>(samples));
    if (rms < 1e-9) {
        return -1000.0;
    }
    return 20.0 * std::log10(rms / 32768.0);
}

EnergyVad::EnergyVad(double threshold_dbfs) : threshold_dbfs_(threshold_dbfs) {}

bool EnergyVad::ProcessFrame(const int16_t* pcm, size_t samples) {
    return FrameRmsDbfs(pcm, samples) > threshold_dbfs_;
}

}  // namespace prosophor::voice
