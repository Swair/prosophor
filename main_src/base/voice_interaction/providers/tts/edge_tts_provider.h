// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "network/websocket_client.h"
#include "providers/tts/tts_provider.h"

#include <cstdint>
#include <string>
#include <vector>

namespace prosophor {

/// WebSocket-based Microsoft Edge TTS provider
/// Directly connects to speech.platform.bing.com, no Python/CLI/ffmpeg needed.
/// MP3 decoding is done in-process via dr_mp3 single-header library.
class EdgeTtsProvider : public TtsProvider {
 public:
    EdgeTtsProvider();
    ~EdgeTtsProvider() override;

    std::string GetProviderName() const override { return "edge-tts"; }

    TtsResponse Synthesize(const TtsRequest& request) override;

 private:
    bool ConnectAndExchange(const std::string& text, const std::string& voice);

    /// Receive all binary frames until turn.end, accumulate MP3 bytes.
    std::vector<uint8_t> ReceiveAllMp3();

    WebSocketClient ws_;
};

}  // namespace prosophor