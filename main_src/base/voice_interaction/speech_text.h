// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>

namespace prosophor::voice {

/// LLM 的"不接话"约定输出（角色 system prompt 教它：桌边闲聊/与己无关
/// 时只输出这个标记）。应用侧识别到 -> 只进对话上下文，不开口。
constexpr const char* kSilenceMarker = "（沉默）";

/// 播报文本净化（纯函数，可单测）：去 URL/markdown/emoji、压空白、按
/// 句末标点截断到 max_chars（UTF-8 码点计）。max_chars == 0 表示不截断。
std::string SanitizeForSpeech(const std::string& reply, size_t max_chars);

/// TTS 前的基础清理：去 emoji/装饰符号、折叠空白（虚精灵旧 CleanTtsText 行为）
std::string NormalizeTtsWhitespace(const std::string& text);

/// 更完整的 TTS 文本净化：去 markdown 记号、emoji、折叠空白
/// （虚精灵旧 SanitizeTtsText 行为）
std::string SanitizeTtsText(const std::string& text);

/// 是否为沉默应答（纯函数）：去空白与两端标点后与标记相同（兼容半角括号写法）
bool IsSilentReply(const std::string& reply);

/// 剥掉嵌在正文里的沉默标记（"（沉默）好的呀" -> "好的呀"）；无标记原样返回
std::string StripSilenceMarker(const std::string& text);

}  // namespace prosophor::voice
