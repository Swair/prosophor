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
        last_error_ = "SDK error " + std::to_string(static_cast<int>(error.error_code)) +
                      ": " + error.message;
        LOG_ERROR("AidVoiceAsrProvider: {}", last_error_.c_str());
        cv_.notify_all();
    }

    /// 当前已完成的 FINAL 计数（write 前取基线，之后等 baseline+1）
    unsigned Finals() {
        std::lock_guard<std::mutex> lock(mutex_);
        return finals_;
    }
    /// 等第 target 个 FINAL 或 SDK 错误；true = 有结果/错误，false = 超时
    bool WaitForResult(unsigned target, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [&] { return finals_ >= target || !last_error_.empty(); });
    }
    std::string LastText() {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_text_;
    }
    void ClearError() {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_.clear();
    }
    std::string LastError() {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_error_;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    unsigned finals_ = 0;
    std::string last_text_;
    std::string last_error_;
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

AsrResponse AidVoiceAsrProvider::AsrProcess(const AsrRequest& request) {
    AsrResponse result;
    if (request.pcm == nullptr || request.sample_size == 0) {
        result.error_msg = "empty PCM";
        return result;
    }
    if (request.options.sample_rate != 16000 || request.options.channels != 1) {
        result.error_msg = "aidvoice ASR only supports 16k mono";
        LOG_ERROR("AidVoiceAsrProvider: {}", result.error_msg.c_str());
        return result;
    }

    std::string error;
    if (!EnsureReady(error)) {
        result.error_msg = error;
        LOG_ERROR("AidVoiceAsrProvider: 未就绪: {}", error.c_str());
        return result;
    }

    std::vector<int16_t> pcm(request.pcm, request.pcm + request.sample_size);

    // 单句一轮：先取 FINAL 基线，write 后等 baseline+1 —— 不会把上一句的结果
    // 当成本句（串行契约下上一句的结果已被消费）
    const unsigned baseline = handle_->bridge->Finals();
    handle_->bridge->ClearError();

    // 调试转储：NUTRIBOT_ASR_DUMP=<目录> 时把每句 PCM 落盘（排查采集质量问题）
    if (const char* dump_dir = std::getenv("NUTRIBOT_ASR_DUMP")) {
        static int dump_seq = 0;
        WriteWav(std::string(dump_dir) + "/utt_" + std::to_string(++dump_seq) + ".wav",
                 pcm, request.options.sample_rate);
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
        result.error_msg = "write failed (ret=" + std::to_string(ret) + ")";
        LOG_ERROR("AidVoiceAsrProvider: {} ({} 采样)", result.error_msg.c_str(), pcm.size());
        return result;
    }
    if (!handle_->bridge->WaitForResult(baseline + 1, cfg_.timeout_ms)) {
        result.error_msg = "wait FINAL timeout (" + std::to_string(cfg_.timeout_ms) + "ms)";
        LOG_ERROR("AidVoiceAsrProvider: {} ({} 采样)", result.error_msg.c_str(), pcm.size());
        return result;
    }
    if (std::string sdk_error = handle_->bridge->LastError(); !sdk_error.empty()) {
        result.error_msg = std::move(sdk_error);
        return result;
    }

    result.text = handle_->bridge->LastText();
    result.success = true;   // 后端调用成功；空 text = 未识别到语音
    LOG_INFO("AidVoiceAsrProvider: {} 采样 -> \"{}\" ({}ms)",
             pcm.size(), result.text.c_str(), ElapsedMs(begin));
    return result;
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
