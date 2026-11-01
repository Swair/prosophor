// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace prosophor::voice {

/// 流式文本分句器：把 LLM 的 streaming delta 攒成完整句子，供 TTS 逐句播报。
/// 无后端依赖，由表现层（如 SDL Sprite）持有；AgentSession 只发 delta。
class TtsChunker {
public:
    /// 丢弃未播报的残留（新一轮正文开始 / 不启用 TTS 时调用）。
    void Reset();

    /// 追加一段流式正文增量。
    void Push(const std::string& delta);

    /// 取出可播报文本：流式进行中只吐完整句子，末尾不完整句保留；
    /// done=true 时把剩余文本全部吐出（流式结束）。
    std::string TakeReady(bool done);

private:
    std::string pending_;
};

}  // namespace prosophor::voice
