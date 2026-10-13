// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/provider_router/tts_provider_router.h"
#ifdef PROSOPHOR_NUTRIBOT
#include "providers/tts/aidvoice_tts_provider.h"
#endif
#ifndef PROSOPHOR_NO_EDGE_TTS
#include "providers/tts/edge_tts_provider.h"
#endif
#include "common/file_utils.h"
#include "common/log_wrapper.h"

namespace prosophor {

// ---- Singleton ----

TtsProviderRouter& TtsProviderRouter::GetInstance() {
    static TtsProviderRouter instance;
    return instance;
}

// ---- Initialize ----

void TtsProviderRouter::Initialize(const ProsophorConfig& config) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    active_backend_ = config.tts.backend.empty() ? "edge-tts" : config.tts.backend;
    LOG_INFO("TtsProviderRouter initializing with backend='{}'", active_backend_);
}

// ---- GetProvider ----

std::shared_ptr<TtsProvider> TtsProviderRouter::GetProvider() {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = providers_.find(active_backend_);
    if (it != providers_.end()) {
        return it->second;
    }

    // Not cached; need to create. Upgrade to write lock.
    lock.unlock();
    {
        std::unique_lock<std::shared_mutex> write_lock(mutex_);
        // Double-check after acquiring write lock
        it = providers_.find(active_backend_);
        if (it != providers_.end()) {
            return it->second;
        }
        // Need config — read from global. The config is static after init.
        auto provider = CreateProvider(active_backend_, ProsophorConfig::GetInstance().tts);
        if (provider) {
            providers_[active_backend_] = provider;
        }
        return provider;
    }
}

// ---- SetBackend ----

void TtsProviderRouter::SetBackend(const std::string& backend) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (active_backend_ == backend) return;
    active_backend_ = backend;
    LOG_INFO("TtsProviderRouter switched backend to '{}'", backend);

    // Pre-create if not cached
    if (providers_.find(backend) == providers_.end()) {
        auto provider = CreateProvider(backend, ProsophorConfig::GetInstance().tts);
        if (provider) {
            providers_[backend] = provider;
        }
    }
}

// ---- GetBackendName ----

std::string TtsProviderRouter::GetBackendName() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return active_backend_;
}

// ---- CreateProvider ----

std::shared_ptr<TtsProvider> TtsProviderRouter::CreateProvider(
    const std::string& backend,
    const TtsConfig& config) {

#ifdef PROSOPHOR_NUTRIBOT
    // 本地 NPU 后端（aidvoice/MeloTTS）：必须排在 edge-tts 可用性判断之前 ——
    // 板上 libcurl 无 websocket API（PROSOPHOR_NO_EDGE_TTS 已定义），
    // 否则会被下面的早退一并挡掉
    if (backend == "aidvoice") {
        if (config.aidvoice_model_dir.empty()) {
            LOG_ERROR("TTS backend 'aidvoice' 但 tts.aidvoice_model_dir 未配置");
            return nullptr;
        }
        AidVoiceTtsConfig aid_cfg;
        aid_cfg.model_dir = ExpandHome(config.aidvoice_model_dir);
        aid_cfg.speed = config.aidvoice_speed;
        LOG_INFO("TtsProviderRouter: aidvoice backend -> {}", aid_cfg.model_dir.c_str());
        return std::make_shared<AidVoiceTtsProvider>(std::move(aid_cfg));
    }
#endif

#ifdef PROSOPHOR_NO_EDGE_TTS
    (void)backend;
    // 系统 libcurl 无 websocket 实验 API(curl_ws_*) -> edge-tts 不可用；
    // TTS 后端未启用，调用方判空处理（升级 libcurl 后移除）
    LOG_WARN("edge-tts 不可用(需 libcurl>=7.86 websocket)，TTS 后端未启用");
    return nullptr;
#else
    if (backend == "edge-tts") {
        return std::make_shared<EdgeTtsProvider>();
    }

    LOG_WARN("Unknown TTS backend '{}', falling back to edge-tts", backend);
    return std::make_shared<EdgeTtsProvider>();
#endif
}

// ---- ReleaseProviders ----

void TtsProviderRouter::ReleaseProviders() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    providers_.clear();   // 析构 provider -> SDK destroy（_Exit 前必须显式调用）
    LOG_INFO("TtsProviderRouter: providers released");
}

}  // namespace prosophor
