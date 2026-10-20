// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/asr_tts_provider_factory.h"

#include "providers/asr/http_asr_provider.h"
#ifdef PROSOPHOR_NUTRIBOT
#include "providers/asr/aidvoice_asr_provider.h"
#include "providers/tts/aidvoice_tts_provider.h"
#endif
#ifndef PROSOPHOR_NO_EDGE_TTS
#include "providers/tts/edge_tts_provider.h"
#endif
#include "common/log_wrapper.h"

namespace prosophor {

namespace {

#if defined(PROSOPHOR_NUTRIBOT)
constexpr bool has_aidvoice_tts = true;
#else
constexpr bool has_aidvoice_tts = false;
#endif

// CMake 在 libcurl < 7.86 或终端形态剔除 edge_tts_provider.cc 时定义该宏
#if defined(PROSOPHOR_NO_EDGE_TTS)
constexpr bool has_edge_tts = false;
#else
constexpr bool has_edge_tts = true;
#endif

// 把配置里的后端规范化为当前构建可处理的名字：
// - 空 -> nutribot 默认 aidvoice，其余默认 edge-tts
// - 桌面配置迁到板端：显式 edge-tts 但构建无 edge -> aidvoice（唯一兼容回退）
// 未知后端不再兜底到 edge-tts，由 CreateProvider 报错返回 nullptr。
std::string NormalizeTtsBackend(std::string backend) {
    if (backend.empty()) {
        if (has_aidvoice_tts) return "aidvoice";
        if (has_edge_tts) return "edge-tts";
        return backend;
    }
    if (!has_edge_tts && backend == "edge-tts" && has_aidvoice_tts) {
        LOG_WARN("TTS backend 'edge-tts' 在当前环境不可用，回退 aidvoice");
        return "aidvoice";
    }
    return backend;
}

}  // namespace

std::unique_ptr<AsrProvider> AsrProviderFactory::CreateProvider(
    const AsrConfig& config) {
#ifdef PROSOPHOR_NUTRIBOT
    // 本地 NPU 后端（aidvoice/SenseVoice）：模型目录缺失是配置错误，
    // 响亮报错并返回 nullptr（调用方判空，不静默回落到不可用的 HTTP 后端）
    if (config.backend == "aidvoice") {
        if (config.aidvoice_cfg.model_dir.empty()) {
            LOG_ERROR("ASR backend 'aidvoice' 但 asr.model_dir 未配置");
            return nullptr;
        }
        return std::make_unique<AidVoiceAsrProvider>(config.aidvoice_cfg);
    }
#endif

    LOG_INFO("AsrProviderFactory: using HTTP ASR backend -> {}", config.server_url);
    return std::make_unique<HttpAsrProvider>(config.server_url);
}

std::unique_ptr<TtsProvider> TtsProviderFactory::CreateProvider(
    const TtsConfig& config) {
    const std::string backend = NormalizeTtsBackend(config.backend);

    if (backend == "aidvoice") {
#ifdef PROSOPHOR_NUTRIBOT
        if (config.aidvoice_cfg.model_dir.empty()) {
            LOG_ERROR("TTS backend 'aidvoice' 但 tts.aidvoice_model_dir 未配置");
            return nullptr;
        }
        return std::make_unique<AidVoiceTtsProvider>(config.aidvoice_cfg);
#else
        LOG_WARN("TTS backend 'aidvoice' 在当前构建不可用");
        return nullptr;
#endif
    }

    if (backend == "edge-tts") {
#ifndef PROSOPHOR_NO_EDGE_TTS
        return std::make_unique<EdgeTtsProvider>();
#else
        LOG_WARN("edge-tts 不可用(需 libcurl>=7.86 websocket)，TTS 后端未启用");
        return nullptr;
#endif
    }

    LOG_WARN("Unknown TTS backend '{}'", backend.empty() ? "（未配置）" : backend.c_str());
    return nullptr;
}

}  // namespace prosophor
