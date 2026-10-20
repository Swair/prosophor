// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "voice_interaction/speech_text.h"

#include <cctype>
#include <cstdint>
#include <string_view>

namespace prosophor::voice {

namespace {

bool IsUtf8Continuation(unsigned char byte) {
    return (byte & 0xC0) == 0x80;
}

// 迭代 UTF-8 码点（不校验合法性，按首字节推长度；ASCII 与合法 UTF-8 均可用）
struct Codepoint {
    uint32_t value = 0;
    int bytes = 1;
};

Codepoint NextCodepoint(const std::string& text, size_t index) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    Codepoint cp;
    cp.value = lead;
    if (lead >= 0xF0) {
        cp.bytes = 4;
        cp.value = lead & 0x07u;
    } else if (lead >= 0xE0) {
        cp.bytes = 3;
        cp.value = lead & 0x0Fu;
    } else if (lead >= 0xC0) {
        cp.bytes = 2;
        cp.value = lead & 0x1Fu;
    }
    for (int k = 1; k < cp.bytes && index + static_cast<size_t>(k) < text.size(); ++k) {
        const unsigned char byte = static_cast<unsigned char>(text[index + static_cast<size_t>(k)]);
        if (!IsUtf8Continuation(byte)) {
            cp.bytes = k;   // 截断的多字节序列：按已读部分收尾
            break;
        }
        cp.value = (cp.value << 6) | (byte & 0x3Fu);
    }
    return cp;
}

bool IsAsciiWhitespace(uint32_t cp) {
    return cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t';
}

bool IsSentenceEnd(uint32_t cp) {
    switch (cp) {
        case 0x3002:  // 。
        case 0xFF01:  // ！
        case 0xFF1F:  // ？
        case 0xFF1B:  // ；
        case '!': case '?': case ';': case '.':
            return true;
        default:
            return false;
    }
}

void TrimTrailingSpaces(std::string& text) {
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
}

// 三套净化规则收敛成选项。默认值保持“只折叠空白 + 去装饰符号”；
// 各公共函数在下面设置自己的差异项。
struct SpeechTextOptions {
    bool drop_model_tokens = false;       // <|...|>
    bool drop_urls = false;               // http:// / https://
    bool drop_markdown_all = false;       // * ` # > _ ~（SanitizeForSpeech）
    bool drop_markdown_tts = false;       // * ` ~ \ | + 条件 #/_（SanitizeTtsText）
    bool drop_controls = false;           // cp < 0x20（空白已在前面处理）
    bool drop_zwj = false;                // U+200D
    bool drop_variation_selectors = false;// U+FE00..U+FE0F
    bool drop_misc_symbols = false;       // U+2600..U+27BF / U+2B00..U+2BFF
    uint32_t drop_astral_from = 0;        // 0 = 不按码点范围丢
    size_t max_chars = 0;                 // 0 = 不截断
    bool truncate_at_sentence = false;
};

bool ShouldDrop(uint32_t cp, const SpeechTextOptions& opt, bool prev_raw_was_space,
                const std::string& out) {
    if (opt.drop_controls && cp < 0x20) {
        return true;
    }
    if (opt.drop_markdown_all) {
        switch (cp) {
            case '*': case '`': case '#': case '>': case '_': case '~':
                return true;
            default:
                break;
        }
    }
    if (opt.drop_markdown_tts) {
        switch (cp) {
            case '*': case '`': case '~': case '\\': case '|':
                return true;
            case '#':
                // 旧 SanitizeTtsText：行首或空白后的 # 丢弃
                if (prev_raw_was_space) return true;
                break;
            case '_':
                // 旧 SanitizeTtsText：输出当前以空格结尾时丢弃
                if (!out.empty() && out.back() == ' ') return true;
                break;
            default:
                break;
        }
    }
    if (opt.drop_zwj && cp == 0x200D) {
        return true;
    }
    if (opt.drop_variation_selectors && cp >= 0xFE00 && cp <= 0xFE0F) {
        return true;
    }
    if (opt.drop_misc_symbols &&
        ((cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF))) {
        return true;
    }
    if (opt.drop_astral_from != 0 && cp >= opt.drop_astral_from) {
        return true;
    }
    return false;
}

std::string SanitizeSpeechText(const std::string& input,
                               const SpeechTextOptions& opt) {
    std::string out;
    out.reserve(input.size());
    size_t codepoints = 0;
    size_t last_sentence_end = std::string::npos;   // out 中的字节位置（含标点）
    size_t last_sentence_end_cp = 0;                // 对应的码点数
    bool pending_space = false;
    bool prev_raw_was_space = true;                 // 供 TtsText 的条件 # 判断

    size_t i = 0;
    while (i < input.size()) {
        if (opt.drop_model_tokens && input.compare(i, 2, "<|") == 0) {
            const size_t close = input.find("|>", i + 2);
            if (close != std::string::npos) {
                i = close + 2;
                prev_raw_was_space = false;
                continue;
            }
        }
        if (opt.drop_urls &&
            (input.compare(i, 7, "http://") == 0 ||
             input.compare(i, 8, "https://") == 0)) {
            while (i < input.size() &&
                   !std::isspace(static_cast<unsigned char>(input[i]))) {
                ++i;
            }
            pending_space = true;
            prev_raw_was_space = true;
            continue;
        }

        const Codepoint cp = NextCodepoint(input, i);
        const size_t char_bytes = static_cast<size_t>(cp.bytes);
        const size_t char_start = i;
        i += char_bytes;

        if (IsAsciiWhitespace(cp.value)) {
            pending_space = true;
            prev_raw_was_space = true;
            continue;
        }
        if (ShouldDrop(cp.value, opt, prev_raw_was_space, out)) {
            prev_raw_was_space = false;
            continue;
        }
        prev_raw_was_space = false;

        // 已达上限而文本未结束：退到最近句末，否则硬截补句号
        if (opt.max_chars > 0 && codepoints >= opt.max_chars) {
            if (opt.truncate_at_sentence && last_sentence_end != std::string::npos &&
                last_sentence_end_cp * 2 >= opt.max_chars) {
                out.resize(last_sentence_end);
            } else {
                out += "。";
            }
            TrimTrailingSpaces(out);
            return out;
        }

        if (pending_space && !out.empty()) {
            out += ' ';
            ++codepoints;
        }
        pending_space = false;

        out.append(input, char_start, char_bytes);
        ++codepoints;
        if (opt.truncate_at_sentence && IsSentenceEnd(cp.value)) {
            last_sentence_end = out.size();
            last_sentence_end_cp = codepoints;
        }
    }
    TrimTrailingSpaces(out);
    return out;
}

// 全角/半角标点：必须逐"序列"比较 —— 多字节标点放进字符集合会按字节展开，
// 误吃掉别处多字节字符的首字节（"（沉默）"的 "（" 首字节 0xEF 与 "，" 相同）
const char* const kPuncts[] = {"。", "，", "！", "？", "；", "、", "：", "…", "—",
                               ".", ",", ";", "!", "?", ":", "-"};

// 剥掉首尾的空白与**一轮**标点（"（沉默）。" -> "（沉默）"）。
// 只剥一轮是刻意为之：IsSilentReply 的等值判断需要标记本体完整；
// 反复剥会把 "（沉默）.-" 这类变体剥成残句，多轮无收益只增误伤面。
std::string TrimEdges(const std::string& text) {
    std::string out = text;
    const size_t begin = out.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    out.erase(0, begin);
    out.erase(out.find_last_not_of(" \t\r\n") + 1);

    for (const char* punct : kPuncts) {
        const size_t len = std::string_view(punct).size();
        if (out.size() >= len && out.compare(0, len, punct) == 0) {
            out.erase(0, len);
            break;
        }
    }
    for (const char* punct : kPuncts) {
        const size_t len = std::string_view(punct).size();
        if (out.size() >= len && out.compare(out.size() - len, len, punct) == 0) {
            out.erase(out.size() - len);
            break;
        }
    }
    return out;
}

}  // namespace

bool IsSilentReply(const std::string& reply) {
    const std::string trimmed = TrimEdges(reply);
    return trimmed == kSilenceMarker || trimmed == "(沉默)" || trimmed == "[沉默]";
}

std::string StripSilenceMarker(const std::string& text) {
    std::string out = text;
    for (const char* marker : {kSilenceMarker, "(沉默)", "[沉默]"}) {
        size_t pos = 0;
        while ((pos = out.find(marker, pos)) != std::string::npos) {
            out.erase(pos, std::string(marker).size());
        }
    }
    return out;
}

std::string SanitizeForSpeech(const std::string& reply, size_t max_chars) {
    SpeechTextOptions opt;
    opt.drop_model_tokens = true;
    opt.drop_urls = true;
    opt.drop_markdown_all = true;
    opt.drop_controls = true;
    opt.drop_zwj = true;
    opt.drop_variation_selectors = true;
    opt.drop_misc_symbols = true;
    opt.drop_astral_from = 0x1F000;
    opt.max_chars = max_chars;
    opt.truncate_at_sentence = true;
    return SanitizeSpeechText(reply, opt);
}

std::string NormalizeTtsWhitespace(const std::string& text) {
    SpeechTextOptions opt;
    opt.drop_variation_selectors = true;
    opt.drop_astral_from = 0x1F000;
    return SanitizeSpeechText(text, opt);
}

std::string SanitizeTtsText(const std::string& text) {
    SpeechTextOptions opt;
    opt.drop_markdown_tts = true;
    opt.drop_astral_from = 0x10000;
    return SanitizeSpeechText(text, opt);
}

}  // namespace prosophor::voice
