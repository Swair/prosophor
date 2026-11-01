// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// 连说并句：把"用户一口气说了几句、ASR 切成几条 final"合并成一次话轮。
//
// VAD 以句尾静音（nutribot 700ms）切句，稍一停顿就会被拆成两条 final。
// 若主循环每 tick 取一条，就会变成"两句话、两个 LLM 周期"——第一句的回复
// 往往已经答非所问。这里拿到第一条后短暂驻留：用户还在说（VAD 正在累积
// 当前句）就一直等，严格半双工不抢话；停口并静默 settle_ms 后一次取走。
// 说满 max_hold_ms 的用户在停口后跳过 settle，立即派发。

#include <cstddef>
#include <string>
#include <vector>

#include "common/time_wrapper.h"

namespace prosophor::nr::app {

struct UtteranceCoalescerTuning {
    int settle_ms = 300;     // 最后一条到齐后的静默窗口（覆盖下一句起说）
    int max_hold_ms = 4000;  // 停口后若已说满该时长，跳过 settle 立即派发
};

class UtteranceCoalescer {
public:
    explicit UtteranceCoalescer(UtteranceCoalescerTuning tuning = {});

    void Add(std::string text, prosophor::SteadyClock::TimePoint now);

    /// 丢弃未派发的片段（进入聆听/待机时清跨窗口残留）
    void Clear();

    bool Empty() const { return parts_.empty(); }
    size_t size() const { return parts_.size(); }

    /// true = 可以取走派发：用户已停口，且静默窗口已过或驻留超过 max_hold。
    /// speech_active = 采集侧 VAD 正在累积当前句（用户还在说，一律不派发）。
    bool ReadyToDispatch(prosophor::SteadyClock::TimePoint now, bool speech_active) const;

    /// 合并（换行分隔）并清空；空时返回空串。
    std::string TakeMerged();

private:
    UtteranceCoalescerTuning tuning_;
    std::vector<std::string> parts_;
    prosophor::SteadyClock::TimePoint first_added_{};
    prosophor::SteadyClock::TimePoint last_added_{};
};

}  // namespace prosophor::nr::app
