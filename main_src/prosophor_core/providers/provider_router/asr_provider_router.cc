// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/provider_router/asr_provider_router.h"
#ifdef PROSOPHOR_NUTRIBOT
#include "providers/asr/aidvoice_asr_provider.h"
#endif
#include "providers/asr/http_asr_provider.h"
#include "config/config.h"
#include "common/file_utils.h"
#include "common/log_wrapper.h"
#include "common/thread_pool.h"

namespace prosophor {

// ---- Singleton ----

AsrProviderRouter& AsrProviderRouter::GetInstance() {
    static AsrProviderRouter instance;
    return instance;
}

// ---- Initialize ----

void AsrProviderRouter::Initialize() {
    LOG_INFO("AsrProviderRouter initializing...");

    // Read server URL from config
    auto& cfg = ProsophorConfig::GetInstance();
    if (!cfg.asr.server_url.empty()) {
        server_url_ = cfg.asr.server_url;
    }

    GetOrCreateProvider();
    LOG_INFO("AsrProviderRouter initialized (server={})", server_url_);
}

// ---- Provider ----

AsrProvider* AsrProviderRouter::GetOrCreateProvider() {
    if (provider_) return provider_.get();

    auto& cfg = ProsophorConfig::GetInstance().asr;
    if (!cfg.server_url.empty()) {
        server_url_ = cfg.server_url;
    }

#ifdef PROSOPHOR_NUTRIBOT
    // 本地 NPU 后端（aidvoice/SenseVoice）：模型目录缺失是配置错误，
    // 响亮报错并返回 nullptr（调用方判空，不静默回落到不可用的 HTTP 后端）
    if (cfg.backend == "aidvoice") {
        if (cfg.model_dir.empty()) {
            LOG_ERROR("ASR backend 'aidvoice' 但 asr.model_dir 未配置");
            return nullptr;
        }
        AidVoiceAsrConfig aid_cfg;
        aid_cfg.model_dir = ExpandHome(cfg.model_dir);
        aid_cfg.echo_ms = cfg.echo_ms;
        aid_cfg.timeout_ms = cfg.timeout_ms;
        aid_cfg.reuse_instance = cfg.reuse_instance;
        provider_ = std::make_unique<AidVoiceAsrProvider>(std::move(aid_cfg));
        LOG_INFO("AsrProviderRouter: aidvoice backend -> {}", aid_cfg.model_dir.c_str());
        return provider_.get();
    }
#endif

    provider_ = std::make_unique<HttpAsrProvider>(server_url_);
    LOG_INFO("AsrProviderRouter: using HTTP ASR backend -> {}", server_url_);
    return provider_.get();
}

// ---- DestroyProvider ----

void AsrProviderRouter::DestroyProvider() {
    if (!provider_) return;
    provider_.reset();   // 析构 -> SDK destroy（_Exit 前必须显式调用）
    LOG_INFO("AsrProviderRouter: provider destroyed");
}

// ---- TranscribeFile (sync) ----

std::string AsrProviderRouter::TranscribeFile(const std::string& audio_path) {
    if (transcribing_.exchange(true)) {
        LOG_WARN("AsrProviderRouter: already transcribing, skipping: {}", audio_path);
        return {};
    }

    auto* provider = GetOrCreateProvider();
    if (provider == nullptr) {
        transcribing_ = false;
        LOG_ERROR("AsrProviderRouter: provider 不可用（配置错误）");
        return {};
    }
    LOG_INFO("AsrProviderRouter: transcribing: {}", audio_path);
    std::string result = provider->TranscribeFile(audio_path);

    transcribing_ = false;

    if (result.empty()) {
        LOG_ERROR("AsrProviderRouter: transcription failed: {}", audio_path);
        if (on_error_) {
            on_error_("transcription failed");
        }
        return {};
    }

    LOG_INFO("AsrProviderRouter: transcription done ({} chars)", result.size());
    if (on_result_) {
        on_result_(result);
    }
    return result;
}

// ---- TranscribePCM ----

std::string AsrProviderRouter::AsrProcess(const std::vector<int16_t>& pcm) {
    if (transcribing_.exchange(true)) {
        LOG_WARN("AsrProviderRouter: already transcribing, skipping PCM ({})", pcm.size());
        return {};
    }

    auto* provider = GetOrCreateProvider();
    if (provider == nullptr) {
        transcribing_ = false;
        LOG_ERROR("AsrProviderRouter: provider 不可用（配置错误）");
        return {};
    }
    LOG_DEBUG("AsrProviderRouter: transcribing PCM ({} samples)", pcm.size());
    std::string result = provider->AsrProcess(pcm);

    transcribing_ = false;

    if (result.empty()) {
        LOG_DEBUG("AsrProviderRouter: PCM transcription empty result");
        if (on_error_) {
            on_error_("PCM transcription failed");
        }
        return {};
    }

    LOG_INFO("AsrProviderRouter: PCM transcription done ({} chars)", result.size());
    if (on_result_) {
        on_result_(result);
    }
    return result;
}

// ---- TranscribeFileAsync ----

void AsrProviderRouter::TranscribeFileAsync(const std::string& audio_path) {
    LOG_INFO("AsrProviderRouter: async transcribe start: {}", audio_path);
    GetGlobalThreadPool().Submit([this, audio_path]() {
        TranscribeFile(audio_path);
    });
}

}  // namespace prosophor
