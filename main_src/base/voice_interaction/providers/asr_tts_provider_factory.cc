// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/asr_tts_provider_factory.h"

#ifdef PROSOPHOR_HAS_AIDVOICE
#include "providers/asr/aidvoice_asr_provider.h"
#include "providers/tts/aidvoice_tts_provider.h"
#endif
#ifdef PROSOPHOR_HAS_SHERPA
#include "providers/asr/sherpa_asr_provider.h"
#include "providers/tts/sherpa_tts_provider.h"
#endif
#ifndef PROSOPHOR_NO_EDGE_TTS
#include "providers/tts/edge_tts_provider.h"
#endif
#include "common/log_wrapper.h"

namespace prosophor {

namespace {

#if defined(PROSOPHOR_HAS_AIDVOICE)
constexpr bool has_aidvoice = true;
#else
constexpr bool has_aidvoice = false;
#endif

#if defined(PROSOPHOR_HAS_SHERPA)
constexpr bool has_sherpa = true;
#else
constexpr bool has_sherpa = false;
#endif

// CMake 在 libcurl < 7.86 或终端形态剔除 edge_tts_provider.cc 时定义该宏
#if defined(PROSOPHOR_NO_EDGE_TTS)
constexpr bool has_edge_tts = false;
#else
constexpr bool has_edge_tts = true;
#endif

// 不做任何自动回退：backend 必须由配置显式指定，且对当前构建可用；
// 不可用就报错返回 nullptr，避免“配错了却悄悄用了别的后端”。
}  // namespace

std::vector<std::string> AsrProviderFactory::AvailableBackends() {
    if (has_sherpa) return {"sherpa-onnx"};
    if (has_aidvoice) return {"aidvoice"};
    return {};
}

std::string AsrProviderFactory::EffectiveBackend(const AsrConfig& config) {
    return config.backend;
}

std::unique_ptr<AsrProvider> AsrProviderFactory::CreateProvider(
    const AsrConfig& config) {
    const std::string& backend = config.backend;

#ifdef PROSOPHOR_HAS_SHERPA
    // 本地离线后端（SenseVoice）：模型缺失在首次识别时给出明确错误
    if (backend == "sherpa-onnx") {
        LOG_INFO("AsrProviderFactory: using local SenseVoice backend -> {}",
                 config.sherpa_model_dir);
        return std::make_unique<SherpaAsrProvider>(config);
    }
#endif

#ifdef PROSOPHOR_HAS_AIDVOICE
    // 本地 NPU 后端（aidvoice/SenseVoice）：模型目录缺失是配置错误，
    // 响亮报错并返回 nullptr（调用方判空）
    if (backend == "aidvoice") {
        if (config.aidvoice_cfg.model_dir.empty()) {
            LOG_ERROR("ASR backend 'aidvoice' 但 asr.model_dir 未配置");
            return nullptr;
        }
        return std::make_unique<AidVoiceAsrProvider>(config.aidvoice_cfg);
    }
#endif

    LOG_ERROR("ASR backend '{}' 不可用（请在 settings.json 显式配置为当前构建支持的后端）",
              backend.empty() ? "（未配置）" : backend.c_str());
    return nullptr;
}

std::vector<std::string> TtsProviderFactory::AvailableBackends() {
    if (has_sherpa) return {"sherpa-onnx"};
    if (has_aidvoice) return {"aidvoice"};
    if (has_edge_tts) return {"edge-tts"};
    return {};
}

std::string TtsProviderFactory::EffectiveBackend(const TtsConfig& config) {
    return config.backend;
}

std::unique_ptr<TtsProvider> TtsProviderFactory::CreateProvider(
    const TtsConfig& config) {
    const std::string& backend = config.backend;

#ifdef PROSOPHOR_HAS_SHERPA
    if (backend == "sherpa-onnx") {
        return std::make_unique<SherpaTtsProvider>(
            config.sherpa_model_dir, config.sherpa_speed,
            config.sherpa_speaker_id, config.sherpa_num_threads);
    }
#endif

    if (backend == "aidvoice") {
#ifdef PROSOPHOR_HAS_AIDVOICE
        if (config.aidvoice_cfg.model_dir.empty()) {
            LOG_ERROR("TTS backend 'aidvoice' 但 tts.aidvoice_model_dir 未配置");
            return nullptr;
        }
        return std::make_unique<AidVoiceTtsProvider>(config.aidvoice_cfg);
#else
        LOG_ERROR("TTS backend 'aidvoice' 在当前构建不可用");
        return nullptr;
#endif
    }

    if (backend == "edge-tts") {
#ifndef PROSOPHOR_NO_EDGE_TTS
        return std::make_unique<EdgeTtsProvider>();
#else
        LOG_ERROR("TTS backend 'edge-tts' 在当前构建不可用(需 libcurl>=7.86 websocket)");
        return nullptr;
#endif
    }

    LOG_ERROR("TTS backend '{}' 不可用（请在 settings.json 显式配置为当前构建支持的后端）",
              backend.empty() ? "（未配置）" : backend.c_str());
    return nullptr;
}

}  // namespace prosophor
