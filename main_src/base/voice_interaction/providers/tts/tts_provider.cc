// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/tts/tts_provider.h"
#include "common/file_utils.h"

namespace prosophor {

TtsResponse TtsProvider::SynthesizeToFile(const TtsRequest& request,
                                           const std::string& output_path) {
    if (output_path.empty()) {
        TtsResponse r;
        r.error_msg = "output_path is empty";
        return r;
    }

    EnsureDirectory(ParentDir(output_path));

    auto result = Synthesize(request);
    if (!result.success) return result;
    if (result.pcm.empty()) {
        TtsResponse r;
        r.error_msg = "no audio data received";
        return r;
    }

    int sr = request.sample_rate > 0 ? request.sample_rate : result.sample_rate;
    if (!WriteWav(output_path, result.pcm, sr)) {
        TtsResponse r;
        r.error_msg = "failed to write WAV file";
        return r;
    }

    TtsResponse r;
    r.success = true;
    r.pcm = std::move(result.pcm);
    r.sample_rate = sr;
    r.channels = request.channels;
    return r;
}

}  // namespace prosophor
