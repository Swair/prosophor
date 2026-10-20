// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "vad/webrtc_vad.h"

namespace prosophor::voice {

WebRtcVad::WebRtcVad() {
    int ret = WebRtcVad_Create(&handle_);
    if (ret != 0 || handle_ == nullptr) return;

    ret = WebRtcVad_Init(handle_);
    if (ret != 0) return;

    WebRtcVad_set_mode(handle_, mode_);
}

WebRtcVad::~WebRtcVad() {
    if (handle_) {
        WebRtcVad_Free(handle_);
    }
}

bool WebRtcVad::ProcessFrame(const int16_t* data, size_t samples) {
    if (!handle_ || samples < 160) return false;

    int ret = WebRtcVad_Process(handle_, kSampleRate,
                                const_cast<int16_t*>(data),
                                static_cast<int>(samples));
    return ret == 1;
}

void WebRtcVad::SetThreshold(float level) {
    int m = 3;
    if (level < 0.5f)      m = 0;
    else if (level < 1.5f) m = 1;
    else if (level < 2.5f) m = 2;
    else                   m = 3;
    mode_ = m;
    if (handle_) {
        WebRtcVad_set_mode(handle_, mode_);
    }
}

void WebRtcVad::Reset() {
    if (handle_) {
        WebRtcVad_Init(handle_);
        WebRtcVad_set_mode(handle_, mode_);
    }
}

}  // namespace prosophor::voice
