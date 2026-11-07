// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/noncopyable.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace prosophor {

/// 一次识别会话的配置（云端口径参考 sibot 的 Asr::__start）。
/// 本地 provider 不支持的字段可忽略；每帧数据不进这里。
struct AsrOptions {
    int sample_rate = 16000;
    int channels = 1;

    std::string language;          // 空 = provider 默认/自动
    std::string prompt;            // 热词/上下文提示

    bool interim_results = true;               // 对应 intermediate_result
    bool punctuation = true;                   // 标点预测
    bool inverse_text_normalization = true;    // 数字转写（ITN）
    int max_sentence_silence_ms = 800;         // 云端断句静音阈值

    std::string customization_id;  // 定制模型
    std::string vocabulary_id;     // 定制热词表
};

/// 一句话识别请求：PCM 所有权归调用方，AsrProcess 同步消费，不得跨调用持有。
struct AsrRequest {
    const int16_t* pcm = nullptr;
    /// 采样点数量（不是字节数；字节数 = sample_size * sizeof(int16_t)）
    size_t sample_size = 0;
    AsrOptions options;
};

/// 识别结果：success = 后端调用成功（text 可能为空 = 未识别到语音）；
/// 传输/解析/SDK 失败时 success=false，原因见 error_msg。
struct AsrResponse {
    bool success = false;
    std::string text;
    std::string error_msg;
    float confidence = 0.0f;   // provider 不支持时为 0
};

/// Abstract base class for ASR backends
class AsrProvider : public Noncopyable {
 public:
    virtual ~AsrProvider() = default;

    virtual std::string GetProviderName() const = 0;

    /// 预热：本地后端可在此完成实例创建/装载，把首句延迟挪到启动期。
    /// 无状态后端（HTTP 等）默认无操作；失败时 error 说明原因。
    virtual bool WarmUp(std::string& error) {
        error.clear();
        return true;
    }

    /// Synchronous transcription: audio file → recognized text.
    /// 默认实现：LoadWav → AsrProcess；需要特殊文件处理的 provider 可覆盖。
    virtual AsrResponse TranscribeFile(const std::string& audio_path);

    /// 一句话 PCM（16k mono int16）→ 文本；失败时 success=false + error_msg
    virtual AsrResponse AsrProcess(const AsrRequest& request) = 0;
};

}  // namespace prosophor
