// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <memory>

#include "common/messages_schema.h"
#include "core/agent_session.h"

namespace aicode {

/// Key decision extracted from conversation
struct KeyDecision {
    std::string type;       // "design_decision" | "code_change" | "unresolved_issue" | "lesson_learned"
    std::string content;
    std::vector<std::string> related_files;
    std::string timestamp;
};

/// Memory consolidation result
struct ConsolidationResult {
    std::string summary;            // Summary of the session/segment
    std::vector<KeyDecision> decisions;  // Key decisions made
    int messages_processed = 0;
    int tokens_estimated = 0;
};

/// Memory consolidation service
/// - Extracts key decisions from conversation
/// - Consolidates experience to Role Memory
class MemoryConsolidationService {
public:
    static MemoryConsolidationService& GetInstance();

    /// Set LLM callback for generating summaries
    using LlmSummaryCallback = std::function<std::string(const std::string& prompt)>;
    void SetLlmCallback(LlmSummaryCallback callback) { llm_callback_ = callback; }

    // =====================
    // 定长总结（每 N 条消息）
    // =====================

    /// Check if messages exceed the threshold for consolidation
    bool NeedsLengthConsolidation(const std::vector<MessageSchema>& messages,
                                   int threshold = 30) const;

    /// Consolidate a segment of messages (e.g., every 30 messages)
    ConsolidationResult ConsolidateSegment(const std::vector<MessageSchema>& messages,
                                            int start_index,
                                            int end_index);

    /// Consolidate segment and save to role memory (convenience method with callback)
    ConsolidationResult ConsolidateSegmentAndSave(
        const AgentSession& session,
        int start_index,
        int end_index,
        LlmSummaryCallback llm_callback);

    // =====================
    // 退出前总结（Session 结束时）
    // =====================

    /// Consolidate entire session before exit (with explicit callback)
    ConsolidationResult ConsolidateSessionExit(
        const AgentSession& session,
        LlmSummaryCallback llm_callback);

    /// Consolidate entire session before exit (uses internal callback if not provided)
    ConsolidationResult ConsolidateSessionExit(const AgentSession& session);

    /// Extract key decisions from messages
    std::vector<KeyDecision> ExtractKeyDecisions(const std::vector<MessageSchema>& messages);

    // =====================
    // 持久化到 Role Memory
    // =====================

    /// Append consolidation result to Role Memory
    void AppendToRoleMemory(const AgentSession& session,
                            const ConsolidationResult& result,
                            const std::string& category = "decisions");

    /// Save daily memory entry to Role Memory directory
    void SaveDailyMemory(const std::string& role_memory_dir,
                         const std::string& content,
                         const std::string& date_str = "");

    // =====================
    // 配置
    // =====================

    /// Get consolidation threshold (messages count)
    int GetConsolidationThreshold() const { return consolidation_threshold_; }

    /// Set consolidation threshold
    void SetConsolidationThreshold(int threshold) { consolidation_threshold_ = threshold; }

    /// Enable/disable auto consolidation
    void SetAutoConsolidationEnabled(bool enabled) { auto_consolidation_enabled_ = enabled; }
    bool IsAutoConsolidationEnabled() const { return auto_consolidation_enabled_; }

private:
    MemoryConsolidationService() = default;
    ~MemoryConsolidationService() = default;

    LlmSummaryCallback llm_callback_;
    int consolidation_threshold_ = 30;  // Trigger consolidation every 30 messages
    bool auto_consolidation_enabled_ = true;

    /// Mutex for thread-safe file writes (per file path)
    std::mutex file_write_mutex_;
    std::unordered_map<std::string, std::unique_ptr<std::mutex>> file_mutexes_;

    /// Build prompt for extracting key decisions
    std::string BuildExtractionPrompt(const std::vector<MessageSchema>& messages);

    /// Build prompt for generating session summary
    std::string BuildSessionSummaryPrompt(const std::vector<MessageSchema>& messages);

    /// Parse key decisions from LLM response
    std::vector<KeyDecision> ParseDecisionsFromResponse(const std::string& response);

    /// Get or create mutex for a specific file path
    std::unique_lock<std::mutex> GetFileLock(const std::string& file_path);
};

}  // namespace aicode
