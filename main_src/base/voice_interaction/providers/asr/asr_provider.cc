// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/asr/asr_provider.h"

#include "common/file_utils.h"
#include "common/log_wrapper.h"

namespace prosophor {

AsrResponse AsrProvider::TranscribeFile(const std::string& audio_path) {
    AsrResponse result;
    const auto pcm = LoadWav(audio_path);
    if (pcm.empty()) {
        result.error_msg = "failed to load WAV: " + audio_path;
        LOG_ERROR("AsrProvider: {}", result.error_msg.c_str());
        return result;
    }
    const AsrRequest request{pcm.data(), pcm.size(), {}};
    return AsrProcess(request);
}

}  // namespace prosophor
