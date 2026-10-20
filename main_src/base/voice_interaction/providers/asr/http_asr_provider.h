// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "providers/asr/asr_provider.h"

#include <string>

namespace prosophor {

/// HTTP client for whisper_server.
/// Sends PCM data to a remote whisper.cpp server for transcription.
/// 当前协议只接收 PCM；AsrOptions 中的 language/prompt 等暂不下发。
class HttpAsrProvider : public AsrProvider {
 public:
    explicit HttpAsrProvider(std::string server_url);

    std::string GetProviderName() const override { return "http-whisper"; }

    /// Transcribe PCM int16 mono 16kHz samples
    AsrResponse AsrProcess(const AsrRequest& request) override;

 private:
    std::string server_url_;
};

}  // namespace prosophor
