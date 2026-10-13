// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/tts/aidvoice_tts_provider.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <vector>

#include "aidlux/aidvoice/aidvoice.hpp"  // SDK 唯一接入点（同 aidgen_provider.cc）
#include "common/log_wrapper.h"

namespace prosophor {

namespace {

using AidLux::AidVoice::ErrorCode;
using AidLux::AidVoice::ErrorMsg;
using AidLux::AidVoice::ResultStatus;
using AidLux::AidVoice::TTSResult;

// 回调桥接：SDK 内部线程累积 PCM 分片并置完成标志，不做阻塞与 IO。
// 生命周期归 SDK（create 后由 destroy 释放），必须以 new 交给 create_tts。
class TtsCallbackBridge final : public AidLux::AidVoice::TTSCallbacks {
public:
    void onResult(const TTSResult& result) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (result.audio_data.empty() && result.status != ResultStatus::TYPE_FINAL) {
            return;
        }
        pcm_.insert(pcm_.end(), result.audio_data.begin(), result.audio_data.end());
        if (result.status == ResultStatus::TYPE_FINAL) {
            ++finals_;
            cv_.notify_all();
        }
    }
    void onStop(const TTSResult& /*result*/) override {}
    void onError(const ErrorMsg& error) override {
        std::lock_guard<std::mutex> lock(mutex_);
        failed_ = true;
        LOG_ERROR("AidVoiceTtsProvider: SDK 回调错误 code={} msg={}",
                  static_cast<int>(error.error_code), error.message.c_str());
        cv_.notify_all();
    }

    /// 开启新一轮：清空累积 PCM（write 前调用，串行契约下上一轮已被取走）
    unsigned BeginRound() {
        std::lock_guard<std::mutex> lock(mutex_);
        pcm_.clear();
        return finals_;
    }
    /// 等第 target 个 FINAL；true = 完成，false = 超时或出错
    bool WaitFinal(unsigned target, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [&] { return finals_ >= target || failed_; });
    }
    bool failed() {
        std::lock_guard<std::mutex> lock(mutex_);
        return failed_;
    }
    /// 取走本轮累积的归一化 float PCM
    std::vector<float> TakePcm() {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::move(pcm_);
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    unsigned finals_ = 0;
    bool failed_ = false;
    std::vector<float> pcm_;
};

long ElapsedMs(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - begin).count();
}

// 归一化 float [-1,1] → int16（实测 MeloTTS 回调即此范围，peak≈0.35）
std::vector<int16_t> FloatToInt16Pcm(const std::vector<float>& input) {
    std::vector<int16_t> out;
    out.reserve(input.size());
    for (const float value : input) {
        const float scaled = std::round(value * 32767.0f);
        out.push_back(static_cast<int16_t>(std::clamp(scaled, -32768.0f, 32767.0f)));
    }
    return out;
}

}  // namespace

// aidvoice 运行时句柄：TTS 实例 + 桥接裸指针（桥接归 SDK 所有，这里只借用）
struct AidVoiceTtsHandle {
    std::unique_ptr<AidLux::AidVoice::AidVoiceTTS> tts;
    TtsCallbackBridge* bridge = nullptr;
};

AidVoiceTtsProvider::AidVoiceTtsProvider(AidVoiceTtsConfig cfg)
    : cfg_(std::move(cfg)) {}

AidVoiceTtsProvider::~AidVoiceTtsProvider() {
    Destroy();
}

bool AidVoiceTtsProvider::WarmUp(std::string& error) {
    error.clear();
    return EnsureReady(error);
}

bool AidVoiceTtsProvider::EnsureReady(std::string& error) {
    if (handle_ != nullptr) {
        return true;
    }
    if (cfg_.model_dir.empty()) {
        error = "aidvoice TTS 模型目录未配置（settings.json 的 tts.aidvoice_model_dir）";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(cfg_.model_dir, ec)) {
        error = "aidvoice TTS 模型目录不存在: " + cfg_.model_dir;
        return false;
    }

    auto* bridge = new TtsCallbackBridge();   // SDK 接管，成功后不可再 delete
    auto tts = AidLux::AidVoice::AidVoiceTTS::create_tts(
        cfg_.model_dir, bridge, AidLux::AidVoice::RunMode::TYPE_NOSTREAM);
    if (tts == nullptr) {
        delete bridge;   // 实例未创建成功 = 回调未被接管，这里负责回收
        error = "create_tts 失败: " + cfg_.model_dir;
        return false;
    }

    tts->set_microphone_id(-1);   // 只合成不播放（播放归调用方音频层）
    if (cfg_.speed > 0.0 && cfg_.speed != 1.0) {
        tts->set_speed(cfg_.speed);
    }
    const int ret = tts->init();
    if (ret != static_cast<int>(ErrorCode::SUCCESS)) {
        tts->destroy();   // 连同回调桥接一起释放
        error = "aidvoice TTS init 失败 (ret=" + std::to_string(ret) + ")";
        return false;
    }

    handle_ = std::make_unique<AidVoiceTtsHandle>();
    handle_->tts = std::move(tts);
    handle_->bridge = bridge;
    LOG_INFO("AidVoiceTtsProvider: 就绪 model={} speed={}",
             cfg_.model_dir.c_str(), cfg_.speed);
    return true;
}

TtsResponse AidVoiceTtsProvider::Synthesize(const TtsRequest& request) {
    TtsResponse response;
    if (request.text.empty()) {
        response.error_msg = "空文本";
        return response;
    }
    std::string error;
    if (!EnsureReady(error)) {
        LOG_ERROR("AidVoiceTtsProvider: 未就绪: {}", error.c_str());
        response.error_msg = error;
        return response;
    }

    const unsigned baseline = handle_->bridge->BeginRound();
    const auto begin = std::chrono::steady_clock::now();
    const int ret = handle_->tts->write({request.text});
    if (ret != static_cast<int>(ErrorCode::SUCCESS)) {
        response.error_msg = "write 失败 (ret=" + std::to_string(ret) + ")";
        LOG_ERROR("AidVoiceTtsProvider: {}", response.error_msg.c_str());
        return response;
    }
    if (!handle_->bridge->WaitFinal(baseline + 1, cfg_.timeout_ms)) {
        response.error_msg = "合成超时或出错";
        LOG_ERROR("AidVoiceTtsProvider: {} ({} 字)", response.error_msg.c_str(),
                  request.text.size());
        return response;
    }
    if (handle_->bridge->failed()) {
        response.error_msg = "SDK 回调报错";
        return response;
    }

    const std::vector<float> floats = handle_->bridge->TakePcm();
    if (floats.empty()) {
        response.error_msg = "合成结果为空";
        return response;
    }

    response.success = true;
    response.pcm = FloatToInt16Pcm(floats);
    response.sample_rate = 48000;   // 模型固定输出（不采用 request.sample_rate）
    response.channels = 1;
    LOG_INFO("AidVoiceTtsProvider: {} 字 -> {} 采样 {:.1f}s ({}ms)",
             request.text.size(), response.pcm.size(),
             static_cast<double>(response.pcm.size()) / response.sample_rate,
             ElapsedMs(begin));
    return response;
}

void AidVoiceTtsProvider::Destroy() {
    if (handle_ == nullptr) {
        return;
    }
    handle_->tts->destroy();   // 释放实例与回调桥接；SDK 内部 join 工作线程
    handle_.reset();
    LOG_INFO("AidVoiceTtsProvider: 已销毁");
}

}  // namespace prosophor
