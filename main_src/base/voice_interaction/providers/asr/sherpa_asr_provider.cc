// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/asr/sherpa_asr_provider.h"

#include <algorithm>
#include <chrono>
#include <climits>
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

std::string Join(const std::string& dir, const char* name) {
  return (std::filesystem::path(dir) / name).string();
}

bool FileExistsAt(const std::string& path) {
  return !path.empty() && prosophor::FileExists(path);
}

}  // namespace

struct SherpaAsrProvider::Impl {
  AsrConfig cfg;
  std::mutex mutex;

  const SherpaOnnxOfflineRecognizer* recognizer = nullptr;
  std::string model_path;
  std::string tokens_path;
  std::string language;

  bool EnsureReady(std::string& error) {
    if (recognizer != nullptr) return true;

    const std::string& dir = cfg.sherpa_model_dir;
    if (dir.empty()) {
      error = "asr.sherpa_model_dir 未配置（SenseVoice 模型目录）";
      return false;
    }
    if (!std::filesystem::is_directory(dir)) {
      error = "SenseVoice 模型目录不存在: " + dir;
      return false;
    }

    const std::string int8_model = Join(dir, "model.int8.onnx");
    const std::string fp32_model = Join(dir, "model.onnx");
    const bool has_int8 = FileExistsAt(int8_model);
    const bool has_fp32 = FileExistsAt(fp32_model);
    if (cfg.sherpa_prefer_fp32 && has_fp32) {
      model_path = fp32_model;
    } else if (has_int8) {
      model_path = int8_model;
      if (cfg.sherpa_prefer_fp32) {
        LOG_WARN("[SherpaAsr] 配置优先 fp32，但目录内没有 model.onnx，改用 model.int8.onnx");
      }
    } else {
      model_path = fp32_model;
    }
    tokens_path = Join(dir, "tokens.txt");
    if (!FileExistsAt(model_path)) {
      error = "SenseVoice 模型文件缺失（model.int8.onnx / model.onnx）: " + dir;
      return false;
    }
    if (!FileExistsAt(tokens_path)) {
      error = "SenseVoice tokens.txt 缺失: " + tokens_path;
      return false;
    }

    language = cfg.language.empty() ? "auto" : cfg.language;

    const auto start = std::chrono::steady_clock::now();

    SherpaOnnxOfflineRecognizerConfig rc;
    std::memset(&rc, 0, sizeof(rc));
    rc.feat_config.sample_rate = 16000;
    rc.feat_config.feature_dim = 80;
    rc.model_config.sense_voice.model = model_path.c_str();
    rc.model_config.sense_voice.language = language.c_str();
    rc.model_config.sense_voice.use_itn =
        cfg.inverse_text_normalization ? 1 : 0;
    rc.model_config.tokens = tokens_path.c_str();
    rc.model_config.num_threads =
        cfg.sherpa_num_threads > 0
            ? cfg.sherpa_num_threads
            : static_cast<int32_t>(
                  std::max(1u, std::min(4u, std::thread::hardware_concurrency())));
    rc.model_config.provider = "cpu";
    rc.model_config.debug = 0;
    rc.decoding_method = "greedy_search";

    recognizer = SherpaOnnxCreateOfflineRecognizer(&rc);
    if (recognizer == nullptr) {
      error = "SherpaOnnxCreateOfflineRecognizer 失败（检查模型目录）: " + dir;
      return false;
    }

    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - start)
                                .count();
    LOG_INFO("[SherpaAsr] ready model={} threads={} lang={} ({}ms)",
             model_path.c_str(), static_cast<int>(rc.model_config.num_threads),
             language.c_str(), static_cast<int>(elapsed_ms));
    return true;
  }

  void Release() {
    if (recognizer != nullptr) {
      SherpaOnnxDestroyOfflineRecognizer(recognizer);
      recognizer = nullptr;
    }
  }

  AsrResponse Decode(const std::vector<float>& samples) {
    AsrResponse response;
    const SherpaOnnxOfflineStream* stream =
        SherpaOnnxCreateOfflineStream(recognizer);
    if (stream == nullptr) {
      response.error_msg = "SherpaOnnxCreateOfflineStream 失败";
      return response;
    }

    SherpaOnnxAcceptWaveformOffline(stream, 16000, samples.data(),
                                    static_cast<int32_t>(samples.size()));
    SherpaOnnxDecodeOfflineStream(recognizer, stream);

    const SherpaOnnxOfflineRecognizerResult* result =
        SherpaOnnxGetOfflineStreamResult(stream);
    if (result == nullptr) {
      response.error_msg = "SherpaOnnxGetOfflineStreamResult 失败";
    } else {
      response.text = result->text != nullptr ? result->text : "";
      response.success = true;
      SherpaOnnxDestroyOfflineRecognizerResult(result);
    }

    SherpaOnnxDestroyOfflineStream(stream);
    return response;
  }
};

SherpaAsrProvider::SherpaAsrProvider(AsrConfig config)
    : impl_(std::make_unique<Impl>()) {
  impl_->cfg = std::move(config);
}

SherpaAsrProvider::~SherpaAsrProvider() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->Release();
}

bool SherpaAsrProvider::WarmUp(std::string& error) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->EnsureReady(error);
}

AsrResponse SherpaAsrProvider::AsrProcess(const AsrRequest& request) {
  AsrResponse response;
  if (request.pcm == nullptr || request.sample_size == 0) {
    response.error_msg = "empty PCM";
    return response;
  }
  if (request.options.sample_rate != 16000 || request.options.channels != 1) {
    response.error_msg = "unsupported PCM format (expect 16k mono)";
    return response;
  }
  if (request.sample_size > static_cast<size_t>(INT_MAX)) {
    response.error_msg = "PCM too long";
    return response;
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::string error;
  if (!impl_->EnsureReady(error)) {
    response.error_msg = std::move(error);
    LOG_ERROR("[SherpaAsr] {}", response.error_msg.c_str());
    return response;
  }

  const auto start = std::chrono::steady_clock::now();

  std::vector<float> samples(request.sample_size);
  for (size_t i = 0; i < request.sample_size; ++i) {
    samples[i] = static_cast<float>(request.pcm[i]) / 32768.0f;
  }

  response = impl_->Decode(samples);

  if (response.success) {
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count();
    LOG_INFO("[SherpaAsr] {} samples -> {} chars ({}ms)",
             static_cast<int>(request.sample_size),
             static_cast<int>(response.text.size()),
             static_cast<int>(elapsed_ms));
  } else {
    LOG_ERROR("[SherpaAsr] {}", response.error_msg.c_str());
  }
  return response;
}

}  // namespace prosophor
