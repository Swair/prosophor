// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/noncopyable.h"
#include "providers/asr/asr_provider.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace prosophor {

/// AsrProviderRouter: singleton facade for speech-to-text
/// Routes to HTTP ASR server backend（或 nutribot 形态的本地 aidvoice 后端）
class AsrProviderRouter : public Noncopyable {
 public:
    static AsrProviderRouter& GetInstance();

    /// Initialize — 按配置预创建 provider（aidvoice 形态下即预热装载）
    void Initialize();

    /// 销毁 provider（释放 SDK 实例与内部工作线程）。
    /// main 的 std::_Exit 会跳过静态析构，退出前必须显式调用（nutribot 形态）。
    void DestroyProvider();

    /// Set server URL (call before Initialize)
    void SetServerUrl(const std::string& url) { server_url_ = url; }

    /// Direct PCM transcription: int16 mono 16kHz samples → text
    std::string AsrProcess(const std::vector<int16_t>& pcm);

    /// Synchronous transcription (audio file)
    std::string TranscribeFile(const std::string& audio_path);

    /// Transcribe an audio file asynchronously (fire-and-forget thread).
    void TranscribeFileAsync(const std::string& audio_path);

    /// Whether transcription is in progress
    bool IsTranscribing() const { return transcribing_; }

    /// Raw ASR provider pointer
    AsrProvider* GetRawProvider() { return provider_.get(); }

    // -- Callbacks --
    using OnResultCallback = std::function<void(const std::string& text)>;
    using OnErrorCallback = std::function<void(const std::string& error)>;

    void SetOnResult(OnResultCallback cb) { on_result_ = std::move(cb); }
    void SetOnError(OnErrorCallback cb) { on_error_ = std::move(cb); }

 private:
    AsrProviderRouter() = default;

    AsrProvider* GetOrCreateProvider();

    std::string server_url_ = "http://127.0.0.1:9100";
    std::unique_ptr<AsrProvider> provider_;

    OnResultCallback on_result_;
    OnErrorCallback on_error_;

    std::atomic<bool> transcribing_{false};
};

}  // namespace prosophor
