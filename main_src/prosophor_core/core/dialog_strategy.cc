// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "core/dialog_strategy.h"

#include <sstream>
#include <algorithm>

#include "common/log_wrapper.h"
#include "common/constants.h"
#include "common/time_wrapper.h"
#include "core/agent_session.h"

namespace prosophor {

// ═══════════════════════════════════════════════════════════════
// 内置预处理策略
// ═══════════════════════════════════════════════════════════════

void DialogStrategy::DefaultPreprocessor(const std::string& processed_message, AgentSession& session) {
    if (processed_message.empty()) return;
    std::string ts = SystemClock::GetCurrentTimestamp();
    session.AddUserMessage("[" + ts + "]\n" + processed_message);
}

void DialogStrategy::SummarizingPreprocessor(const std::string& processed_message, AgentSession& session) {
    if (processed_message.empty()) return;

    std::string ts = SystemClock::GetCurrentTimestamp();
    std::string user_content = "[" + ts + "]\n";

    auto* role = session.GetRole();
    if (role && role->enable_summary) {
        std::string running_summary;
        const auto& msgs = session.GetMessages();
        for (int i = static_cast<int>(msgs.size()) - 1; i >= 0; i--) {
            if (msgs[i].role == "assistant" && !msgs[i].summary.empty()) {
                running_summary = msgs[i].summary;
                break;
            }
        }
        if (!running_summary.empty()) {
            user_content += "[摘要]\n" + running_summary + "\n\n";
        }
        user_content += processed_message;
        user_content += "\n\n[总结要求]\n请按贝尔曼衰减方式生成对话摘要：本轮新内容详细保留（高权重 γ→1），历史摘要随时间衰减（低权重 γ^n），关键决策和未解决问题不衰减。将完整摘要放在回复末尾的[摘要]中。";
    } else {
        user_content += processed_message;
    }

    session.AddUserMessage(user_content);
}

// ═══════════════════════════════════════════════════════════════
// 静态压缩工具
// ═══════════════════════════════════════════════════════════════

bool DialogStrategy::NeedsCompaction(const std::vector<MessageSchema>& messages, const Config& config) {
    if (static_cast<int>(messages.size()) > config.max_messages) {
        LOG_DEBUG("Compaction needed: {} messages > max {}", messages.size(), config.max_messages);
        return true;
    }
    int tokens = EstimateTokens(messages);
    if (tokens > config.max_tokens) {
        LOG_DEBUG("Compaction needed: {} tokens > max {}", tokens, config.max_tokens);
        return true;
    }
    return false;
}

namespace {

// 分字符集估算 token：CJK 码点 ≈ 1 token，其余按 4 字符/token（英文经验值）。
// 原来一律 chars/4 —— 对中文低估约 4 倍：小上下文模型（nutribot 用 cl2048，
// 实测 role.context_window=2048）因此迟迟不触发压缩，直到 KV 溢出后模型退化
// 成复读（实测：约 30 轮后只回 "I."）。
int EstimateTextTokens(const std::string& text) {
    size_t multibyte = 0;   // 多字节码点数（UTF-8：非续接字节各计一个码点）
    size_t singlebyte = 0;
    for (const unsigned char c : text) {
        if ((c & 0xC0) == 0x80) continue;
        if (c >= 0x80) {
            ++multibyte;
        } else {
            ++singlebyte;
        }
    }
    return static_cast<int>(multibyte + singlebyte / kCharsPerTokenEstimate);
}

}  // namespace

int DialogStrategy::EstimateTokens(const std::vector<MessageSchema>& messages) {
    int total = 0;
    for (const auto& msg : messages) {
        for (const auto& block : msg.content) {
            if (block.type == "text" || block.type == "thinking") {
                total += EstimateTextTokens(block.text);
            } else if (block.type == "tool_use") {
                total += EstimateTextTokens(block.name) +
                         EstimateTextTokens(block.input.dump());
            } else if (block.type == "tool_result") {
                total += EstimateTextTokens(block.content);
            }
        }
    }
    return total;
}

std::vector<MessageSchema> DialogStrategy::KeepRecentMessages(
    const std::vector<MessageSchema>& messages, int keep_count) {
    if (static_cast<int>(messages.size()) <= keep_count) {
        return messages;
    }
    auto start = messages.end() - keep_count;
    return std::vector<MessageSchema>(start, messages.end());
}

// ═══════════════════════════════════════════════════════════════
// 工厂
// ═══════════════════════════════════════════════════════════════

std::shared_ptr<DialogStrategy> DialogStrategy::CreateDefault() {
    return std::make_shared<DialogStrategy>();
}

// ═══════════════════════════════════════════════════════════════
// 应用策略
// ═══════════════════════════════════════════════════════════════

void DialogStrategy::Preprocess(const std::string& message, AgentSession& session) const {
    auto* role = session.GetRole();
    bool enable_summary = role && role->enable_summary;

    if (message.empty()) return;

    if (preprocessor_) {
        preprocessor_(message, session);
    } else if (enable_summary) {
        SummarizingPreprocessor(message, session);
    } else {
        DefaultPreprocessor(message, session);
    }
}

void DialogStrategy::CompactIfNeeded(AgentSession& session) const {
    auto* role = session.GetRole();
    if (!role) return;
    int ctx = role->context_window;
    int reserve = role->max_tokens > 0 ? role->max_tokens : 4096;
    // Safety margin: reserve 90% of available space for the prompt,
    // leaving 10% headroom for chat template tokens and estimate error
    int limit = static_cast<int>((ctx - reserve) * 0.9);
    if (limit <= 0) return;

    auto messages = session.GetMessages();
    int estimated = EstimateTokens(messages);
    if (estimated <= limit) return;

    int keep = config_.keep_recent;
    LOG_WARN("Context window limit: ~{} tokens > {} (ctx={}, reserve={}), truncating to last {} messages",
             estimated, limit, ctx, reserve, keep);

    // Binary search for the largest keep count that fits within limit
    int lo = 1, hi = keep;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (EstimateTokens(KeepRecentMessages(messages, mid)) <= limit)
            lo = mid;
        else
            hi = mid - 1;
    }
    auto kept = KeepRecentMessages(messages, lo);
    session.CompactHistory(kept, "");

    // 把被压缩掉的旧消息刷入持久化日志
    session.FlushToDisk();

    LOG_WARN("Compacted to {} messages (~{} tokens) to fit ctx={} limit={}", lo, EstimateTokens(kept), ctx, limit);
}

void DialogStrategy::Compact(AgentSession& session, const MessageSchema& processed_message) const {
    if (compactor_) {
        compactor_(processed_message, session);
    }
}

// ═══════════════════════════════════════════════════════════════
// LLM 压缩
// ═══════════════════════════════════════════════════════════════

std::string DialogStrategy::BuildCompactionPrompt(const std::vector<MessageSchema>& messages) {
    std::ostringstream prompt;
    prompt << "I need you to summarize the conversation history so far. "
           << "The summary will be used as context for future conversation turns.\n\n"
           << "Please include:\n"
           << "1. Key decisions and conclusions reached\n"
           << "2. Code changes that were made or discussed\n"
           << "3. Files that were modified\n"
           << "4. Any unresolved issues or TODOs\n"
           << "5. Important context about the task or codebase\n\n"
           << "Keep the summary concise but comprehensive. "
           << "Aim for 200-500 words.\n\n";

    for (const auto& msg : messages) {
        std::string role = (msg.role == "user") ? "User" :
                           (msg.role == "assistant") ? "Assistant" : msg.role;

        for (const auto& block : msg.content) {
            if (block.type == "text") {
                prompt << role << ": " << block.text << "\n\n";
            } else if (block.type == "tool_use") {
                prompt << role << " [Called: " << block.name << "]\n";
            } else if (block.type == "tool_result") {
                std::string preview = block.content.substr(0, 200);
                if (block.content.size() > 200) preview += "...";
                prompt << "[Result: " << preview << "]\n\n";
            }
        }
    }

    return prompt.str();
}

std::string DialogStrategy::GenerateSummary(
    const std::vector<MessageSchema>& old_messages,
    std::function<std::string(const std::string& prompt)> llm_callback) {

    if (old_messages.empty()) return "";

    std::ostringstream transcript;
    transcript << "Previous conversation:\n\n";

    for (const auto& msg : old_messages) {
        std::string role = (msg.role == "user") ? "User" :
                           (msg.role == "assistant") ? "Assistant" : msg.role;

        for (const auto& block : msg.content) {
            if (block.type == "text" || block.type == "thinking") {
                transcript << role << ": " << block.text << "\n\n";
            } else if (block.type == "tool_use") {
                transcript << role << " [Using tool: " << block.name << "]\n";
            } else if (block.type == "tool_result") {
                transcript << "[Tool result]\n\n";
            }
        }
    }

    transcript << "\n" << config_.summary_prompt;

    if (llm_callback) {
        try {
            std::string summary = llm_callback(transcript.str());
            LOG_INFO("Generated summary of {} old messages", old_messages.size());
            return summary;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to generate summary: {}", e.what());
        }
    }

    std::ostringstream fallback;
    fallback << "[Previous conversation with " << old_messages.size()
             << " messages was summarized. Key details may have been omitted.]";
    return fallback.str();
}

DialogStrategy::Result DialogStrategy::CompactWithLLM(
    const std::vector<MessageSchema>& messages,
    std::function<std::string(const std::string& prompt)> llm_callback) {

    Result result;

    if (messages.empty()) {
        LOG_DEBUG("No messages to compact");
        return result;
    }

    if (!NeedsCompaction(messages, config_)) {
        LOG_DEBUG("Compaction not needed, returning all messages");
        result.kept_messages = messages;
        return result;
    }

    int messages_to_keep = std::min(config_.keep_recent, static_cast<int>(messages.size()));
    int messages_to_compress = static_cast<int>(messages.size()) - messages_to_keep;

    if (messages_to_compress <= 0) {
        LOG_DEBUG("No messages need compression");
        result.kept_messages = messages;
        return result;
    }

    std::vector<MessageSchema> old_messages(messages.begin(), messages.begin() + messages_to_compress);
    std::vector<MessageSchema> recent_messages(messages.begin() + messages_to_compress, messages.end());

    LOG_INFO("Compacting {} old messages, keeping {} recent messages",
             messages_to_compress, messages_to_keep);

    std::string summary;
    if (config_.strategy == Strategy::Summary || config_.strategy == Strategy::Hybrid) {
        summary = GenerateSummary(old_messages, llm_callback);
    }

    result.messages_removed = messages_to_compress;
    if (!summary.empty()) result.summary = summary;
    for (const auto& msg : recent_messages) {
        result.kept_messages.push_back(msg);
    }

    int original_tokens = EstimateTokens(old_messages);
    int summary_tokens = EstimateTextTokens(summary);   // 同口径（CJK 按码点）
    result.tokens_saved = original_tokens - summary_tokens;

    compaction_count_++;

    LOG_INFO("Compaction complete: removed {} messages, saved ~{} tokens",
             result.messages_removed, result.tokens_saved);

    return result;
}

DialogStrategy::Result DialogStrategy::CompressToTokenLimit(
    const std::vector<MessageSchema>& messages,
    int max_tokens,
    std::function<std::string(const std::string& prompt)> llm_callback) {

    if (messages.empty()) return Result();

    int current_tokens = EstimateTokens(messages);
    if (current_tokens <= max_tokens) {
        Result result;
        result.kept_messages = messages;
        return result;
    }

    std::vector<MessageSchema> working_messages = messages;

    while (EstimateTokens(working_messages) > max_tokens &&
           static_cast<int>(working_messages.size()) > config_.keep_recent) {
        working_messages.erase(working_messages.begin());
    }

    std::string summary;
    if (EstimateTokens(working_messages) > max_tokens && llm_callback) {
        int keep = std::min(config_.keep_recent, static_cast<int>(working_messages.size()) / 2);
        std::vector<MessageSchema> to_summarize(working_messages.begin(),
                                           working_messages.end() - keep);
        std::vector<MessageSchema> to_keep(working_messages.end() - keep, working_messages.end());
        summary = GenerateSummary(to_summarize, llm_callback);
        working_messages = to_keep;
    }

    Result result;
    result.summary = summary;
    result.kept_messages = working_messages;
    result.messages_removed = static_cast<int>(messages.size()) - static_cast<int>(working_messages.size());
    result.tokens_saved = current_tokens - EstimateTokens(working_messages);

    LOG_INFO("Compressed to token limit: {} -> {} tokens, removed {} messages",
             current_tokens, EstimateTokens(working_messages), result.messages_removed);

    return result;
}

}  // namespace prosophor
