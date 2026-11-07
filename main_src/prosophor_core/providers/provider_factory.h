// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>

#include "config/config.h"
#include "providers/asr_tts_provider_factory.h"
#include "providers/llm/llm_provider.h"

namespace prosophor {

/// LLM provider 工厂（纯静态，不持有实例；所有权归调用方）。
/// 只负责 type -> 具体实现的选择，不参与缓存或生命周期管理。
/// ASR/TTS 工厂见 base/voice_interaction/providers/asr_tts_provider_factory.h。

class LlmProviderFactory {
public:
    /// 创建 provider；未知 type 返回 nullptr。
    static std::unique_ptr<LLMProvider> CreateProvider(
        const std::string& type,
        const LlmProviderConfig& config);
};

}  // namespace prosophor
