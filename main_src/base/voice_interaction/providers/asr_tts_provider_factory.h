// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "voice_interaction/voice_config.h"
#include "providers/asr/asr_provider.h"
#include "providers/tts/tts_provider.h"

namespace prosophor {

/// ASR/TTS provider 工厂（纯静态，不持有实例；所有权归调用方）。
/// 只负责 backend -> 具体实现的选择，不参与缓存或生命周期管理。
/// LLM 工厂见 prosophor_core/providers/provider_factory.h。

class AsrProviderFactory {
public:
    /// 创建 provider；未知/配置错误返回 nullptr。
    static std::unique_ptr<AsrProvider> CreateProvider(const AsrConfig& config);

    /// 当前构建可选的 ASR 后端（本地离线优先）。
    static std::vector<std::string> AvailableBackends();

    /// 配置中的 backend 规范化后的实际后端（用于 UI 显示 / interim 判定）。
    static std::string EffectiveBackend(const AsrConfig& config);
};

class TtsProviderFactory {
public:
    /// 创建 provider；未知/配置错误返回 nullptr。
    static std::unique_ptr<TtsProvider> CreateProvider(const TtsConfig& config);

    /// 当前构建可选的 TTS 后端（本地离线优先）。
    static std::vector<std::string> AvailableBackends();

    /// 配置中的 backend 规范化后的实际后端（用于 UI 显示）。
    static std::string EffectiveBackend(const TtsConfig& config);
};

}  // namespace prosophor
