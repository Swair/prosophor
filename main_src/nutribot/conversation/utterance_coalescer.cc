// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "conversation/utterance_coalescer.h"

#include <utility>

namespace prosophor::nr::app {

UtteranceCoalescer::UtteranceCoalescer(UtteranceCoalescerTuning tuning)
    : tuning_(tuning) {}

void UtteranceCoalescer::Add(std::string text,
                             prosophor::SteadyClock::TimePoint now) {
    if (parts_.empty()) first_added_ = now;
    last_added_ = now;
    parts_.push_back(std::move(text));
}

void UtteranceCoalescer::Clear() {
    parts_.clear();
    first_added_ = {};
    last_added_ = {};
}

bool UtteranceCoalescer::ReadyToDispatch(prosophor::SteadyClock::TimePoint now,
                                         bool speech_active) const {
    if (parts_.empty()) return false;
    // 严格半双工：用户还在说就一直等，max_hold 也不能抢话（否则闭麦会截断用户）
    if (speech_active) return false;
    // 停口后：已经聊满 max_hold 的不再等 settle，立即派发
    if (now - first_added_ >= prosophor::Milliseconds(tuning_.max_hold_ms)) {
        return true;
    }
    return now - last_added_ >= prosophor::Milliseconds(tuning_.settle_ms);
}

std::string UtteranceCoalescer::TakeMerged() {
    std::string merged;
    for (const auto& part : parts_) {
        if (!merged.empty()) merged += "\n";
        merged += part;
    }
    parts_.clear();
    return merged;
}

}  // namespace prosophor::nr::app
