// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// AidVoice ASR Provider：本地 NPU 语音识别直连（官方 aidvoice-sdk）
//
// 与 aidgen_provider 同为仓库的 SDK 接入点：头文件不引 SDK 头（不透明句柄
// 前置声明），aidvoice.hpp 只在本 .cc 出现 —— 非 nutribot 形态靠 CMake 过滤
// 本 .cc 即可在无 SDK 环境编译。
//
// 实例语义（2026-10-12 板上实测，探针见 ~/aidvoice/probe/asr_probe.cpp）：
//   - create 一次、连续多句 write 成立；每句约 200~300ms（编码器定窗开销，
//     与音频长短弱相关）
//   - write(const char*, size_t) 重载缺唤醒通知：写完无回调，仅 destroy 才吐
//     结果 ⇒ 本实现只用 write(std::vector<float>)，int16 数值直接进 float
//     （官方 test_asr_vector.cpp 同款语义，实测识别正确）
//
// 线程契约：AsrProcess/TranscribeFile 必须单线程串行调用 —— SDK 内部是单
// 缓冲（并发 write 会被并成一轮）。SDK 回调在内部线程执行，桥接类只置标志
// 并 notify，不做阻塞与 IO。

#include <memory>
#include <string>
#include <vector>

#include "providers/asr/asr_provider.h"

namespace prosophor {

// aidvoice 运行时句柄（ASR 实例 + 回调桥接，定义于 .cc）
struct AidVoiceAsrHandle;

struct AidVoiceAsrConfig {
    std::string model_dir;       // 模型目录（含 config.json 与 *.aidem）
    int echo_ms = 15000;         // 单轮推理窗口；SenseVoice 上限 15000
    int timeout_ms = 20000;      // 单句等待 FINAL 上限（超时返回空串，不阻塞主循环）
    bool reuse_instance = true;  // false = 每句重建实例（1.6s/句，E1 反例时的退路开关）
};

class AidVoiceAsrProvider final : public AsrProvider {
public:
    explicit AidVoiceAsrProvider(AidVoiceAsrConfig cfg);
    ~AidVoiceAsrProvider() override;

    std::string GetProviderName() const override { return "aidvoice"; }

    /// 音频文件识别（要求 16k 单声道 16bit wav；内部 LoadWav 后走 PCM 路径）
    std::string TranscribeFile(const std::string& audio_path) override;

    /// 一句 utterance PCM → 文本（阻塞至 FINAL 或超时）；未就绪/失败返回空串
    std::string AsrProcess(const std::vector<int16_t>& pcm) override;

    /// 预创建 + init（启动期调用，把约 1.6s 装载开销挪出首句）。
    /// @param error 失败时填错误描述；成功清空
    /// @return 就绪返回 true
    bool WarmUp(std::string& error);

    /// 显式销毁 SDK 实例（main 的 std::_Exit 跳过静态析构，退出前必须调用）
    void Destroy();

private:
    bool EnsureReady(std::string& error);

    AidVoiceAsrConfig cfg_;
    std::unique_ptr<AidVoiceAsrHandle> handle_;
};

}  // namespace prosophor
