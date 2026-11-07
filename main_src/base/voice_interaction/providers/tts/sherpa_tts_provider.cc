// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/tts/sherpa_tts_provider.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "sherpa-onnx/c-api/c-api.h"

#include "common/file_utils.h"
#include "common/log_wrapper.h"

namespace prosophor {

namespace {

bool FileExistsAt(const std::string& path) {
  return !path.empty() && prosophor::FileExists(path);
}

std::string Join(const std::string& dir, const char* name) {
  return (std::filesystem::path(dir) / name).string();
}

// MeloTTS 目录布局：model.onnx + tokens.txt + lexicon.txt [+ dict/ + 规则 FST]
struct ModelPaths {
  std::string model;
  std::string tokens;
  std::string lexicon;
  std::string rule_fsts;
};

bool ResolveMeloTtsPaths(const std::string& model_dir, ModelPaths& out,
                         std::string& error) {
  if (model_dir.empty()) {
    error = "tts.sherpa_model_dir 未配置（MeloTTS 模型目录）";
    return false;
  }
  if (!std::filesystem::is_directory(model_dir)) {
    error = "MeloTTS 模型目录不存在: " + model_dir;
    return false;
  }

  out.model = Join(model_dir, "model.onnx");
  out.tokens = Join(model_dir, "tokens.txt");
  out.lexicon = Join(model_dir, "lexicon.txt");
  if (!FileExistsAt(out.model)) {
    error = "MeloTTS 模型文件缺失: " + out.model;
    return false;
  }
  if (!FileExistsAt(out.tokens)) {
    error = "MeloTTS tokens.txt 缺失: " + out.tokens;
    return false;
  }
  if (!FileExistsAt(out.lexicon)) {
    error = "MeloTTS lexicon.txt 缺失: " + out.lexicon;
    return false;
  }

  // 中文文本正则（日期/数字/电话）；缺失不致命，只是 tts 文本规整能力变弱
  std::string rule_fsts;
  for (const char* name : {"date.fst", "number.fst", "phone.fst"}) {
    auto path = Join(model_dir, name);
    if (!FileExistsAt(path)) continue;
    if (!rule_fsts.empty()) rule_fsts += ",";
    rule_fsts += path;
  }
  out.rule_fsts = std::move(rule_fsts);
  return true;
}

}  // namespace

struct SherpaTtsProvider::Impl {
  std::string model_dir;
  float speed = 1.0f;
  int speaker_id = 0;
  int num_threads = 0;

  std::mutex mutex;
  const SherpaOnnxOfflineTts* tts = nullptr;

  // 创建时 config 里的 const char* 指向这些 string，必须活到 create 返回
  ModelPaths paths;

  bool EnsureReady(std::string& error) {
    if (tts != nullptr) return true;
    if (!ResolveMeloTtsPaths(model_dir, paths, error)) return false;

    const auto start = std::chrono::steady_clock::now();

    SherpaOnnxOfflineTtsConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.model.vits.model = paths.model.c_str();
    cfg.model.vits.tokens = paths.tokens.c_str();
    cfg.model.vits.lexicon = paths.lexicon.c_str();
    cfg.model.num_threads =
        num_threads > 0
            ? num_threads
            : static_cast<int32_t>(
                  std::max(1u, std::min(4u, std::thread::hardware_concurrency())));
    cfg.model.provider = "cpu";
    cfg.model.debug = 0;
    cfg.rule_fsts = paths.rule_fsts.empty() ? nullptr : paths.rule_fsts.c_str();
    cfg.max_num_sentences = 2;

    tts = SherpaOnnxCreateOfflineTts(&cfg);
    if (tts == nullptr) {
      error = "SherpaOnnxCreateOfflineTts 失败（检查模型目录）: " + model_dir;
      return false;
    }
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - start)
                                .count();
    LOG_INFO("[SherpaTts] ready model={} threads={} ({}ms)", model_dir.c_str(),
             static_cast<int>(cfg.model.num_threads), static_cast<int>(elapsed_ms));
    return true;
  }

  void Release() {
    if (tts != nullptr) {
      SherpaOnnxDestroyOfflineTts(tts);
      tts = nullptr;
    }
  }
};

SherpaTtsProvider::SherpaTtsProvider(std::string model_dir, float speed,
                                     int speaker_id, int num_threads)
    : impl_(std::make_unique<Impl>()) {
  impl_->model_dir = std::move(model_dir);
  impl_->speed = speed > 0.0f ? speed : 1.0f;
  impl_->speaker_id = speaker_id;
  impl_->num_threads = num_threads;
}

SherpaTtsProvider::~SherpaTtsProvider() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->Release();
}

bool SherpaTtsProvider::WarmUp(std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->EnsureReady(error);
}

TtsResponse SherpaTtsProvider::Synthesize(const TtsRequest& request) {
  TtsResponse response;
  if (request.text.empty()) {
    response.error_msg = "空文本";
    return response;
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::string error;
  if (!impl_->EnsureReady(error)) {
    response.error_msg = std::move(error);
    LOG_ERROR("[SherpaTts] {}", response.error_msg.c_str());
    return response;
  }

  const auto start = std::chrono::steady_clock::now();

  SherpaOnnxGenerationConfig gen;
  std::memset(&gen, 0, sizeof(gen));
  gen.silence_scale = 0.2f;
  gen.speed = impl_->speed;
  gen.sid = impl_->speaker_id;

  const SherpaOnnxGeneratedAudio* audio =
      SherpaOnnxOfflineTtsGenerateWithConfig(impl_->tts, request.text.c_str(),
                                             &gen, nullptr, nullptr);
  if (audio == nullptr) {
    response.error_msg = "sherpa-onnx TTS 生成失败";
    LOG_ERROR("[SherpaTts] {}", response.error_msg.c_str());
    return response;
  }

  if (audio->n <= 0 || audio->samples == nullptr) {
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    response.error_msg = "合成结果为空";
    return response;
  }

  response.pcm.resize(static_cast<size_t>(audio->n));
  for (int32_t i = 0; i < audio->n; ++i) {
    const float sample = std::clamp(audio->samples[i], -1.0f, 1.0f);
    response.pcm[static_cast<size_t>(i)] =
        static_cast<int16_t>(std::lround(sample * 32767.0f));
  }
  response.sample_rate = audio->sample_rate;
  response.channels = 1;
  response.success = true;

  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
  LOG_INFO("[SherpaTts] synth {} chars -> {} samples @{}Hz ({}ms)",
           static_cast<int>(request.text.size()), audio->n, audio->sample_rate,
           static_cast<int>(elapsed_ms));

  SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
  return response;
}

}  // namespace prosophor
