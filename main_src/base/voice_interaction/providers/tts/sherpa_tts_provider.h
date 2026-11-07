// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// SherpaTtsProvider：sherpa-onnx 本地 TTS（MeloTTS / VITS）。
//
// 与 aidvoice provider 同为"本地离线"实现：阻塞式合成、返回 int16 PCM，
// 播放/缓存/队列全部由 VoiceInteraction 负责，provider 不碰音频设备。
// SDK 头只在 .cc 出现，header 保持与平台无关。

#include <memory>
#include <string>

#include "providers/tts/tts_provider.h"

namespace prosophor {

class SherpaTtsProvider final : public TtsProvider {
 public:
  SherpaTtsProvider(std::string model_dir, float speed, int speaker_id,
                    int num_threads);
  ~SherpaTtsProvider() override;

  std::string GetProviderName() const override { return "sherpa-onnx"; }

  bool WarmUp(std::string& error) override;

  TtsResponse Synthesize(const TtsRequest& request) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace prosophor
