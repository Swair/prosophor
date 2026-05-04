// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "common/tts_speaker.h"
#include "common/log_wrapper.h"
#include "common/file_utils.h"
#include "platform/platform.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace prosophor {

namespace {

constexpr const char* TTS_OUTPUT_DIR = "assets/tts_cache/";
constexpr int TTS_TIMEOUT_MS = 30000;

std::string MakeOutputPath() {
    static int seq = 0;
    seq++;
    return std::string(TTS_OUTPUT_DIR) + "tts_" + std::to_string(seq) + ".wav";
}

}  // namespace

TtsSpeaker& TtsSpeaker::GetInstance() {
    static TtsSpeaker instance;
    return instance;
}

void TtsSpeaker::Initialize() {
    EnsureDirectory(TTS_OUTPUT_DIR);
    LOG_INFO("TtsSpeaker initialized (voice: {}).", voice_);
}

void TtsSpeaker::SetOnSynthesized(OnSynthesizedCallback cb) {
    on_synthesized_ = cb;
}

void TtsSpeaker::Speak(const std::string& text) {
    if (text.empty() || speaking_) return;

    std::thread([this, text]() {
        SpeakAsync(text);
    }).detach();
}

void TtsSpeaker::SpeakAsync(const std::string& text) {
    speaking_ = true;
    std::string wav_path = Synthesize(text);
    speaking_ = false;

    if (!wav_path.empty() && on_synthesized_) {
        on_synthesized_(wav_path);
    }
}

bool TtsSpeaker::IsSpeaking() const {
    return speaking_;
}

std::string TtsSpeaker::Synthesize(const std::string& text) {
    std::string wav_path = MakeOutputPath();

    std::string cmd = "edge-tts"
        " -t " + platform::ShellEscape(text) +
        " -v " + platform::ShellEscape(voice_) +
        " --write-media " + platform::ShellEscape(wav_path);

    LOG_DEBUG("TTS command: edge-tts ...");

    auto result = platform::RunCommandWithOutput(cmd, TTS_TIMEOUT_MS / 1000);

    if (result.exit_code == -2) {
        LOG_WARN("TtsSpeaker: edge-tts timeout");
    } else if (result.exit_code != 0) {
        LOG_ERROR("TtsSpeaker: edge-tts exit code={}", result.exit_code);
    }

    if (FileExists(wav_path)) {
        LOG_INFO("TtsSpeaker: synthesized {} ({} bytes)", wav_path,
                 static_cast<int>(std::filesystem::file_size(wav_path)));
        return wav_path;
    }

    LOG_ERROR("TtsSpeaker: output file not found: {}", wav_path);
    return "";
}

}  // namespace prosophor
