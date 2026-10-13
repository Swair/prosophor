// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "providers/tts/tts_provider.h"
#include "config/config.h"

namespace prosophor {

/// TtsProviderRouter: config-driven TTS provider creation and access.
///
/// Follows the same pattern as LlmProviderRouter:
/// - Initialize() reads TtsConfig and creates the appropriate backend
/// - GetProvider() returns the active provider instance
/// - Supports runtime provider switching via SetBackend()
class TtsProviderRouter {
public:
    static TtsProviderRouter& GetInstance();

    /// Initialize from global config. Creates the provider specified by
    /// config.tts.backend.
    void Initialize(const ProsophorConfig& config);

    /// Get the active TTS provider. Returns nullptr if not initialized.
    std::shared_ptr<TtsProvider> GetProvider();

    /// Switch to a different backend at runtime (e.g. "edge-tts" → "volc").
    /// Creates the provider if not already cached.
    void SetBackend(const std::string& backend);

    /// Get the active backend name.
    std::string GetBackendName() const;

    /// 释放全部已创建的 provider（含 SDK 实例的内部线程）。
    /// main 的 std::_Exit 会跳过静态析构，退出前必须显式调用（nutribot 形态）。
    void ReleaseProviders();

private:
    TtsProviderRouter() = default;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<TtsProvider>> providers_;
    std::string active_backend_;

    /// Create a provider instance by backend name with the given config.
    std::shared_ptr<TtsProvider> CreateProvider(
        const std::string& backend,
        const TtsConfig& config);
};

}  // namespace prosophor
