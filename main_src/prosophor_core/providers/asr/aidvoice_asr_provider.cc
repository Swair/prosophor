// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "providers/asr/aidvoice_asr_provider.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <mutex>

#include "aidlux/aidvoice/aidvoice.hpp"  // SDK 唯一接入点（同 aidgen_provider.cc）
#include "common/file_utils.h"
#include "common/log_wrapper.h"

namespace prosophor {

namespace {

using AidLux::AidVoice::AsrResult;
using AidLux::AidVoice::ErrorCode;
using AidLux::AidVoice::ErrorMsg;
using AidLux::AidVoice::ResultStatus;

// 回调桥接：SDK 内部线程只做「置标志 + 存文本 + notify」，不做阻塞与 IO。
// 生命周期归 SDK（create 后由 destroy 释放），必须以 new 交给 create_asr。
class AsrCallbackBridge final : public AidLux::AidVoice::ASRCallbacks {
public:
    void onResult(const AsrResult& result) override {
        if (result.status != ResultStatus::TYPE_FINAL) {
            return;   // v1 只消费整句结果（PARTIAL 丢弃）
        }
        std::lock_guard<std::mutex> lock(mutex_);
        ++finals_;
        last_text_ = result.text;
        cv_.notify_all();
    }
    void onStop(const AsrResult& /*result*/) override {}
    void onError(const ErrorMsg& error) override {
        std::lock_guard<std::mutex> lock(mutex_);
        LOG_ERROR("AidVoiceAsrProvider: SDK 回调错误 code={} msg={}",
                  static_cast<int>(error.error_code), error.message.c_str());
        cv_.notify_all();
    }

    /// 当前已完成的 FINAL 计数（write 前取基线，之后等 baseline+1）
    unsigned Finals() {
        std::lock_guard<std::mutex> lock(mutex_);
        return finals_;
    }
    /// 等第 target 个 FINAL；true = 已到达，false = 超时
    bool WaitForFinal(unsigned target, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [&] { return finals_ >= target; });
    }
    std::string LastText() {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_text_;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    unsigned finals_ = 0;
    std::string last_text_;
};

// 单句等待结果的耗时统计（毫秒）
long ElapsedMs(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - begin).count();
}

}  // namespace

// aidvoice 运行时句柄：ASR 实例 + 桥接裸指针（桥接归 SDK 所有，这里只借用，
// Destroy 后随 handle_ 一起失效）
struct AidVoiceAsrHandle {
    std::unique_ptr<AidLux::AidVoice::AidVoiceASR> asr;
    AsrCallbackBridge* bridge = nullptr;
};

AidVoiceAsrProvider::AidVoiceAsrProvider(AidVoiceAsrConfig cfg)
    : cfg_(std::move(cfg)) {}

AidVoiceAsrProvider::~AidVoiceAsrProvider() {
    Destroy();
}

bool AidVoiceAsrProvider::WarmUp(std::string& error) {
    error.clear();
    return EnsureReady(error);
}

bool AidVoiceAsrProvider::EnsureReady(std::string& error) {
    if (handle_ != nullptr) {
        return true;
    }
    if (cfg_.model_dir.empty()) {
        error = "aidvoice ASR 模型目录未配置（settings.json 的 asr.model_dir）";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(cfg_.model_dir, ec)) {
        error = "aidvoice ASR 模型目录不存在: " + cfg_.model_dir;
        return false;
    }

    auto* bridge = new AsrCallbackBridge();   // SDK 接管，成功后不可再 delete
    auto asr = AidLux::AidVoice::AidVoiceASR::create_asr(
        cfg_.model_dir, bridge, AidLux::AidVoice::RunMode::TYPE_NOSTREAM);
    if (asr == nullptr) {
        // 实例未创建成功 = 回调未被接管，这里负责回收
        delete bridge;
        error = "create_asr 失败: " + cfg_.model_dir;
        return false;
    }

    asr->set_echo_ms(cfg_.echo_ms);
    asr->set_special_tokens(false);
    const int ret = asr->init();
    if (ret != static_cast<int>(ErrorCode::SUCCESS)) {
        asr->destroy();   // 连同回调桥接一起释放
        error = "aidvoice ASR init 失败 (ret=" + std::to_string(ret) + ")";
        return false;
    }

    handle_ = std::make_unique<AidVoiceAsrHandle>();
    handle_->asr = std::move(asr);
    handle_->bridge = bridge;
    LOG_INFO("AidVoiceAsrProvider: 就绪 model={} echo_ms={}",
             cfg_.model_dir.c_str(), cfg_.echo_ms);
    return true;
}

std::string AidVoiceAsrProvider::AsrProcess(const std::vector<int16_t>& pcm) {
    if (pcm.empty()) {
        return {};
    }
    std::string error;
    if (!EnsureReady(error)) {
        LOG_ERROR("AidVoiceAsrProvider: 未就绪: {}", error.c_str());
        return {};
    }

    // 单句一轮：先取 FINAL 基线，write 后等 baseline+1 —— 不会把上一句的结果
    // 当成本句（串行契约下上一句的结果已被消费）
    const unsigned baseline = handle_->bridge->Finals();

    // 调试转储：NUTRIBOT_ASR_DUMP=<目录> 时把每句 PCM 落盘（排查采集质量问题）
    if (const char* dump_dir = std::getenv("NUTRIBOT_ASR_DUMP")) {
        static int dump_seq = 0;
        WriteWav(std::string(dump_dir) + "/utt_" + std::to_string(++dump_seq) + ".wav",
                 pcm, 16000);
    }

    // int16 数值直接进 float（官方 test_asr_vector.cpp 同款语义，实测正确）
    std::vector<float> samples;
    samples.reserve(pcm.size());
    for (const int16_t sample : pcm) {
        samples.push_back(static_cast<float>(sample));
    }

    const auto begin = std::chrono::steady_clock::now();
    const int ret = handle_->asr->write(std::move(samples));
    if (ret != static_cast<int>(ErrorCode::SUCCESS)) {
        LOG_ERROR("AidVoiceAsrProvider: write 失败 (ret={}, {} 采样)", ret, pcm.size());
        return {};
    }
    if (!handle_->bridge->WaitForFinal(baseline + 1, cfg_.timeout_ms)) {
        LOG_ERROR("AidVoiceAsrProvider: 等待识别超时 ({}ms, {} 采样)",
                  cfg_.timeout_ms, pcm.size());
        return {};
    }

    std::string text = handle_->bridge->LastText();
    LOG_INFO("AidVoiceAsrProvider: {} 采样 -> \"{}\" ({}ms)",
             pcm.size(), text.c_str(), ElapsedMs(begin));
    return text;
}

std::string AidVoiceAsrProvider::TranscribeFile(const std::string& audio_path) {
    const auto pcm = LoadWav(audio_path);
    if (pcm.empty()) {
        LOG_ERROR("AidVoiceAsrProvider: 读取音频失败 {}", audio_path.c_str());
        return {};
    }
    return AsrProcess(pcm);
}

void AidVoiceAsrProvider::Destroy() {
    if (handle_ == nullptr) {
        return;
    }
    handle_->asr->destroy();   // 释放实例与回调桥接；SDK 内部 join 工作线程
    handle_.reset();
    LOG_INFO("AidVoiceAsrProvider: 已销毁");
}

}  // namespace prosophor
