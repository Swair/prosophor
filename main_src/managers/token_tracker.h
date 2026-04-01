// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include "core/messages_schema.h"

namespace aicode {

/// Token usage statistics for a single model
struct TokenStats {
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
    double cost_usd = 0;

    // Cache-related tokens
    int cache_read_input_tokens = 0;
    int cache_creation_input_tokens = 0;

    // Web search requests
    int web_search_requests = 0;

    // Code changes tracking
    int lines_added = 0;
    int lines_removed = 0;

    // API timing (milliseconds)
    int64_t api_duration_ms = 0;
    int64_t api_duration_without_retries_ms = 0;
    int retry_count = 0;

    void Add(int prompt, int completion);
    double CalculateCost(double input_rate, double output_rate);
};

/// Extended token usage statistics with caching and advanced metrics
struct ExtendedTokenStats : public TokenStats {
    // Session-level metrics
    int session_count = 0;
    int64_t total_session_duration_ms = 0;
    int tool_invocations = 0;
    int64_t tool_duration_ms = 0;

    // FPS metrics for UI performance (optional)
    double fps_average = 0;
    double fps_low_1_pct = 0;

    void MergeFrom(const TokenStats& other);
};

/// Token usage tracking across models and sessions
class TokenTracker {
public:
    static TokenTracker& GetInstance();

    /// Record token usage
    void RecordUsage(const std::string& model, int prompt_tokens, int completion_tokens);

    /// Record extended usage with cache and timing info
    void RecordExtendedUsage(const std::string& model,
                              int prompt_tokens, int completion_tokens,
                              int cache_read_input_tokens,
                              int cache_creation_input_tokens,
                              int web_search_requests,
                              int64_t api_duration_ms,
                              int64_t api_duration_without_retries_ms,
                              int retry_count,
                              double cost_usd);

    /// Record code changes
    void RecordCodeChanges(int lines_added, int lines_removed);

    /// Record tool invocation
    void RecordToolInvocation(int64_t duration_ms);

    /// Record FPS metrics
    void RecordFpsMetrics(double average_fps, double low_1_pct_fps);

    /// Get stats for a specific model
    TokenStats GetModelStats(const std::string& model) const;

    /// Get extended stats for a specific model
    ExtendedTokenStats GetExtendedModelStats(const std::string& model) const;

    /// Get total stats across all models
    TokenStats GetTotalStats() const;

    /// Get extended total stats
    ExtendedTokenStats GetExtendedTotalStats() const;

    /// Get all model stats
    std::unordered_map<std::string, TokenStats> GetAllStats() const;

    /// Get estimated cost for a model
    double GetEstimatedCost(const std::string& model) const;

    /// Get total estimated cost
    double GetTotalEstimatedCost() const;

    /// Get cache statistics
    int GetTotalCacheReadTokens() const;
    int GetTotalCacheCreationTokens() const;

    /// Get web search statistics
    int GetTotalWebSearchRequests() const;

    /// Get code change statistics
    int GetTotalLinesAdded() const;
    int GetTotalLinesRemoved() const;

    /// Get API timing statistics
    int64_t GetTotalApiDurationMs() const;
    int64_t GetTotalApiDurationWithoutRetriesMs() const;

    /// Get tool invocation statistics
    int GetTotalToolInvocations() const;
    int64_t GetTotalToolDurationMs() const;

    /// Reset all stats
    void Reset();

    /// Set cost rates for a model (per 1K tokens)
    void SetCostRate(const std::string& model, double input_rate, double output_rate);

    /// Get model usage for all models (for cost display)
    std::unordered_map<std::string, ExtendedTokenStats> GetAllExtendedStats() const;

    /// Export stats to JSON
    nlohmann::json ToJson() const;

    /// Import stats from JSON
    void FromJson(const nlohmann::json& json);

    /// Save stats to file
    void SaveToFile(const std::string& filepath);

    /// Load stats from file
    void LoadFromFile(const std::string& filepath);

    /// Format total cost for display
    std::string FormatTotalCost() const;

    /// Format model usage summary
    std::string FormatModelUsage() const;

private:
    TokenTracker() = default;
    ~TokenTracker() = default;

    std::unordered_map<std::string, TokenStats> model_stats_;
    std::unordered_map<std::string, ExtendedTokenStats> extended_stats_;
    std::unordered_map<std::string, std::pair<double, double>> cost_rates_;  // input, output per 1K tokens
    mutable std::unordered_map<std::string, TokenStats> cached_totals_;
    mutable std::unordered_map<std::string, ExtendedTokenStats> cached_extended_totals_;
    mutable bool totals_dirty = true;

    static std::unordered_map<std::string, std::pair<double, double>> default_cost_rates_;
};

/// Token counting utilities
class TokenCounter {
public:
    /// Estimate token count for text (rough approximation: 1 token ≈ 4 chars)
    static int EstimateTokens(const std::string& text);

    /// Count tokens using tiktoken-style counting (simplified)
    static int CountTokens(const std::string& text, const std::string& model = "claude-3");

    /// Count tokens for a message with content blocks
    static int CountMessageTokens(const std::vector<ContentSchema>* content, const std::string& model = "claude-3");
};

}  // namespace aicode
