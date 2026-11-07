// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/noncopyable.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace prosophor {

/// Per-call parameters for TTS synthesis.
struct TtsRequest {
    std::string text;
    std::string voice = "zh-CN-XiaoxiaoNeural";
    int sample_rate = 24000;
    int channels = 1;
};

/// Result of a synthesis call.
struct TtsResponse {
    bool success = false;
    std::string error_msg;
    std::vector<int16_t> pcm;
    int sample_rate = 0;
    int channels = 0;
};

/// Abstract base class for TTS backends.
class TtsProvider : public Noncopyable {
 public:
    virtual ~TtsProvider() = default;

    virtual std::string GetProviderName() const = 0;

    /// 预热：本地后端可在此完成实例创建/装载，把首播延迟挪到启动期。
    /// 无状态后端（edge-tts 等）默认无操作；失败时 error 说明原因。
    virtual bool WarmUp(std::string& error) {
        error.clear();
        return true;
    }

    /// Full synthesis: text in → PCM audio out. No streaming.
    virtual TtsResponse Synthesize(const TtsRequest& request) = 0;

    /// Convenience: synthesize to WAV file.
    virtual TtsResponse SynthesizeToFile(const TtsRequest& request,
                                          const std::string& output_path);
};

}  // namespace prosophor
