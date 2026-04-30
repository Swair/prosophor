// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "core/compact_service.h"

#include <sstream>
#include <algorithm>

#include "common/log_wrapper.h"
#include "common/constants.h"

namespace prosophor {

CompactService& CompactService::GetInstance() {
    static CompactService instance;
    return instance;
}

void CompactService::Initialize(const CompactConfig& config) {
    config_ = config;
    LOG_DEBUG("CompactService initialized: max_messages={}, keep_recent={}, max_tokens={}, strategy={}",
             config_.max_messages, config_.keep_recent, config_.max_tokens,
             config_.strategy == CompactStrategy::Summary ? "Summary" :
             config_.strategy == CompactStrategy::Truncate ? "Truncate" : "Hybrid");
}

bool CompactService::NeedsCompaction(const std::vector<MessageSchema>& messages) const {
    if (!auto_compact_enabled_) {
        return false;
    }

    // Check message count
    if (static_cast<int>(messages.size()) > config_.max_messages) {
        LOG_DEBUG("Compaction needed: {} messages > max {}", messages.size(), config_.max_messages);
        return true;
    }

    // Check token count
    int tokens = EstimateTokens(messages);
    if (tokens > config_.max_tokens) {
        LOG_DEBUG("Compaction needed: {} tokens > max {}", tokens, config_.max_tokens);
        return true;
    }

    return false;
}

// ES.45: Use symbolic constant from constants.h
// kCharsPerTokenEstimate = 4
int CompactService::EstimateTokens(const std::vector<MessageSchema>& messages) const {
    int total_chars = 0;

    for (const auto& msg : messages) {
        for (const auto& block : msg.content) {
            if (block.type == "text" || block.type == "thinking") {
                total_chars += block.text.size();
            } else if (block.type == "tool_use") {
                total_chars += block.name.size() + block.input.dump().size();
            } else if (block.type == "tool_result") {
                total_chars += block.content.size();
            }
        }
    }

    // Rough estimate: 1 token per 4 characters
    return total_chars / kCharsPerTokenEstimate;
}

std::vector<MessageSchema> CompactService::KeepRecentMessages(
    const std::vector<MessageSchema>& messages, int keep_count) {

    if (static_cast<int>(messages.size()) <= keep_count) {
        return messages;
    }

    // Keep the most recent messages
    auto start = messages.end() - keep_count;
    return std::vector<MessageSchema>(start, messages.end());
}

std::string CompactService::GenerateSummary(
    const std::vector<MessageSchema>& old_messages,
    std::function<std::string(const std::string& prompt)> llm_callback) {

    if (old_messages.empty()) {
        return "";
    }

    // Build conversation transcript for summarization
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

    // Call LLM to generate summary
    if (llm_callback) {
        try {
            std::string summary = llm_callback(transcript.str());
            LOG_INFO("Generated summary of {} old messages", old_messages.size());
            return summary;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to generate summary: {}", e.what());
        }
    }

    // Fallback: simple truncation summary
    std::ostringstream fallback;
    fallback << "[Previous conversation with " << old_messages.size()
             << " messages was summarized. Key details may have been omitted.]";
    return fallback.str();
}

std::string CompactService::BuildCompactionPrompt(const std::vector<MessageSchema>& messages) {
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

CompactResult CompactService::Compact(
    const std::vector<MessageSchema>& messages,
    std::function<std::string(const std::string& prompt)> llm_callback) {

    CompactResult result;

    if (messages.empty()) {
        LOG_DEBUG("No messages to compact");
        return result;
    }

    // Check if compaction is needed based on configured thresholds
    if (!NeedsCompaction(messages)) {
        LOG_DEBUG("Compaction not needed, returning all messages");
        result.kept_messages = messages;
        return result;
    }

    // Determine how many messages to keep
    int messages_to_keep = std::min(config_.keep_recent, static_cast<int>(messages.size()));
    int messages_to_compress = static_cast<int>(messages.size()) - messages_to_keep;

    if (messages_to_compress <= 0) {
        LOG_DEBUG("No messages need compression");
        result.kept_messages = messages;
        return result;
    }

    // Split messages into old (to compress) and recent (to keep)
    std::vector<MessageSchema> old_messages(messages.begin(), messages.begin() + messages_to_compress);
    std::vector<MessageSchema> recent_messages(messages.begin() + messages_to_compress, messages.end());

    LOG_INFO("Compacting {} old messages, keeping {} recent messages",
             messages_to_compress, messages_to_keep);

    // Generate summary based on strategy
    std::string summary;

    if (config_.strategy == CompactStrategy::Summary ||
        config_.strategy == CompactStrategy::Hybrid) {
        summary = GenerateSummary(old_messages, llm_callback);
    }

    // Build result
    result.messages_removed = messages_to_compress;

    if (!summary.empty()) {
        result.summary = summary;
    }

    // Add recent messages
    for (const auto& msg : recent_messages) {
        result.kept_messages.push_back(msg);
    }

    // Estimate tokens saved
    int original_tokens = EstimateTokens(old_messages);
    int summary_tokens = summary.empty() ? 0 : static_cast<int>(summary.size()) / kCharsPerTokenEstimate;
    result.tokens_saved = original_tokens - summary_tokens;

    compaction_count_.fetch_add(1, std::memory_order_relaxed);

    LOG_INFO("Compaction complete: removed {} messages, saved ~{} tokens",
             result.messages_removed, result.tokens_saved);

    return result;
}

CompactResult CompactService::CompressToTokenLimit(
    const std::vector<MessageSchema>& messages,
    int max_tokens,
    std::function<std::string(const std::string& prompt)> llm_callback) {

    if (messages.empty()) {
        return CompactResult();
    }

    int current_tokens = EstimateTokens(messages);

    if (current_tokens <= max_tokens) {
        CompactResult result;
        result.kept_messages = messages;
        return result;
    }

    // Iteratively compress until under limit
    std::vector<MessageSchema> working_messages = messages;

    while (EstimateTokens(working_messages) > max_tokens &&
           static_cast<int>(working_messages.size()) > config_.keep_recent) {

        // Remove oldest message
        working_messages.erase(working_messages.begin());
    }

    // If still over limit, try summarization
    std::string summary;
    if (EstimateTokens(working_messages) > max_tokens && llm_callback) {
        // Find messages to summarize (excluding recent ones)
        int keep = std::min(config_.keep_recent, static_cast<int>(working_messages.size()) / 2);
        std::vector<MessageSchema> to_summarize(working_messages.begin(),
                                           working_messages.end() - keep);
        std::vector<MessageSchema> to_keep(working_messages.end() - keep, working_messages.end());

        summary = GenerateSummary(to_summarize, llm_callback);
        working_messages = to_keep;
    }

    CompactResult result;
    result.summary = summary;
    result.kept_messages = working_messages;
    result.messages_removed = static_cast<int>(messages.size()) - static_cast<int>(working_messages.size());
    result.tokens_saved = current_tokens - EstimateTokens(working_messages);

    LOG_INFO("Compressed to token limit: {} -> {} tokens, removed {} messages",
             current_tokens, EstimateTokens(working_messages), result.messages_removed);

    return result;
}

}  // namespace prosophor
