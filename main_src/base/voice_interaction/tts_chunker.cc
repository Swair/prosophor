// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "voice_interaction/tts_chunker.h"

#include <cctype>
#include <vector>

namespace prosophor::voice {

namespace {

size_t CountCodepoints(const std::string& text) {
    size_t count = 0;
    for (const unsigned char c : text) {
        if ((c & 0xC0) != 0x80) ++count;
    }
    return count;
}

/// 按中英文句末/句中停顿切分，保留标点；最小句长按码点，避免碎片化播报。
std::vector<std::string> SplitSentences(const std::string& text, size_t min_chars) {
    std::vector<std::string> segments;
    std::string buf;
    for (size_t i = 0; i < text.size(); i++) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        bool is_split = false;
        if (c < 0x80) {
            buf += text[i];
            if (c == '.') {
                // 3.14 / v1.2 这类小数点不切句
                const bool decimal = i > 0 && i + 1 < text.size() &&
                    std::isdigit(static_cast<unsigned char>(text[i - 1])) &&
                    std::isdigit(static_cast<unsigned char>(text[i + 1]));
                is_split = !decimal;
            } else {
                is_split = (c == '!' || c == '?' || c == ',' || c == '\n');
            }
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            unsigned char c2 = static_cast<unsigned char>(text[i + 1]);
            unsigned char c3 = static_cast<unsigned char>(text[i + 2]);
            buf.append(text, i, 3);
            i += 2;
            is_split = ((c == 0xE3 && c2 == 0x80 && c3 == 0x82) ||
                        (c == 0xEF && c2 == 0xBC && (c3 == 0x81 || c3 == 0x8C || c3 == 0x9F)));
        } else {
            buf += text[i];
        }
        if (is_split && CountCodepoints(buf) >= min_chars) {
            segments.push_back(buf);
            buf.clear();
        }
    }
    if (!buf.empty()) {
        segments.push_back(buf);
    }
    return segments;
}

}  // namespace

void TtsChunker::Reset() {
    pending_.clear();
}

void TtsChunker::Push(const std::string& delta) {
    pending_ += delta;
}

std::string TtsChunker::TakeReady(bool done) {
    if (pending_.empty()) return {};

    if (done) {
        std::string out = std::move(pending_);
        pending_.clear();
        return out;
    }

    auto segments = SplitSentences(pending_, 5);
    if (segments.size() < 2) {
        // 只有一段 = 还没有可确认的完整句，继续等后续 delta
        return {};
    }

    std::string out;
    for (size_t i = 0; i + 1 < segments.size(); ++i) {
        out += segments[i];
    }
    pending_ = std::move(segments.back());   // 最后一段可能不完整，留到下次
    return out;
}

}  // namespace prosophor::voice
