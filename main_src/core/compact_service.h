// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

#include "core/messages_schema.h"

namespace aicode {

/// Result of context compaction
struct CompactResult {
    std::string summary;           // Generated summary of compressed content (for system prompt)
    int tokens_saved = 0;          // Estimated tokens saved
    int messages_removed = 0;      // Number of messages removed
    std::vector<MessageSchema> kept_messages;  // Messages that were kept (excludes system)
};

/// Context compaction strategy
enum class CompactStrategy {
    Summary,      // Generate AI summary of old messages
    Truncate,     // Keep only recent N messages
    Hybrid        // Summary + keep recent
};

/// Configuration for context compaction
struct CompactConfig {
    CompactStrategy strategy = CompactStrategy::Hybrid;
    int max_messages = 100;         // Max messages before triggering compaction
    int keep_recent = 20;           // Number of recent messages to always keep
    int max_tokens = 100000;        // Max total tokens before compaction
    std::string summary_prompt;     // Custom prompt for summary generation

    static CompactConfig Default() {
        CompactConfig cfg;
        cfg.summary_prompt =
            "Please provide a concise summary of the conversation so far, "
            "including: key decisions made, code changes discussed, "
            "and any unresolved issues. Keep it brief but comprehensive.";
        return cfg;
    }
};

/// Context compaction service
class CompactService {
public:
    static CompactService& GetInstance();

    /// Initialize with configuration
    void Initialize(const CompactConfig& config = CompactConfig::Default());

    /// Check if compaction is needed
    bool NeedsCompaction(const std::vector<MessageSchema>& messages) const;

    /// Get token count estimate for messages
    int EstimateTokens(const std::vector<MessageSchema>& messages) const;

    /// Perform context compaction
    CompactResult Compact(const std::vector<MessageSchema>& messages,
                          std::function<std::string(const std::string& prompt)> llm_callback);

    /// Compress messages to fit within token limit
    CompactResult CompressToTokenLimit(const std::vector<MessageSchema>& messages,
                                        int max_tokens,
                                        std::function<std::string(const std::string& prompt)> llm_callback);

    /// Get current configuration
    const CompactConfig& GetConfig() const { return config_; }

    /// Set configuration
    void SetConfig(const CompactConfig& config) { config_ = config; }

    /// Check if auto-compaction is enabled
    bool IsAutoCompactEnabled() const { return auto_compact_enabled_; }

    /// Enable/disable auto-compaction
    void SetAutoCompactEnabled(bool enabled) { auto_compact_enabled_ = enabled; }

    /// Get compaction count (for telemetry)
    int GetCompactionCount() const { return compaction_count_; }

    /// Keep only recent N messages (public for testing)
    std::vector<MessageSchema> KeepRecentMessages(const std::vector<MessageSchema>& messages,
                                             int keep_count);

private:
    CompactService() = default;
    ~CompactService() = default;

    CompactConfig config_;
    bool auto_compact_enabled_ = true;
    int compaction_count_ = 0;

    /// Generate summary of old messages using LLM
    std::string GenerateSummary(const std::vector<MessageSchema>& old_messages,
                                 std::function<std::string(const std::string& prompt)> llm_callback);

    /// Build compaction prompt
    std::string BuildCompactionPrompt(const std::vector<MessageSchema>& messages);
};

}  // namespace aicode
