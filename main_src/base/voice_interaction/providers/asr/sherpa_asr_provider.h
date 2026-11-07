// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// SherpaAsrProvider：sherpa-onnx 本地 ASR（SenseVoice）。
//
// 一句话识别语义与 aidvoice provider 一致：同步阻塞、PCM 所有权归调用方、
// 不跨调用持有音频缓冲。SDK 头只在 .cc 出现。

#include <memory>

#include "providers/asr/asr_provider.h"
#include "voice_interaction/voice_config.h"

namespace prosophor {

class SherpaAsrProvider final : public AsrProvider {
 public:
  explicit SherpaAsrProvider(AsrConfig config);
  ~SherpaAsrProvider() override;

  std::string GetProviderName() const override { return "sherpa-onnx"; }

  bool WarmUp(std::string& error) override;

  AsrResponse AsrProcess(const AsrRequest& request) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace prosophor
