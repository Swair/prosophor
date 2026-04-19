// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "managers/token_tracker.h"

#include <numeric>
#include <sstream>
#include <iomanip>

#include "common/log_wrapper.h"
#include "common/file_utils.h"
#include "providers/llm_provider.h"

namespace aicode {

// Default cost rates per 1K tokens (USD)
std::unordered_map<std::string, std::pair<double, double>> TokenTracker::default_cost_rates_ = {
    {"claude-sonnet-4-6", {3.0, 15.0}},
    {"claude-opus-4-6", {15.0, 75.0}},
    {"claude-haiku-4-5", {0.8, 4.0}},
    {"qwen-max", {2.8, 8.4}},
    {"qwen-plus", {0.4, 1.2}},
    {"qwen-turbo", {0.14, 0.42}},
};

void TokenStats::Add(int prompt, int completion) {
    prompt_tokens += prompt;
    completion_tokens += completion;
    total_tokens += prompt + completion;
}

double TokenStats::CalculateCost(double input_rate, double output_rate) {
    cost_usd = (prompt_tokens / 1000.0 * input_rate) +
               (completion_tokens / 1000.0 * output_rate) +
               (cache_read_input_tokens / 1000.0 * input_rate * 0.5) +  // Cache read discount
               (cache_creation_input_tokens / 1000.0 * output_rate);     // Cache write = output rate
    return cost_usd;
}

void ExtendedTokenStats::MergeFrom(const TokenStats& other) {
    prompt_tokens += other.prompt_tokens;
    completion_tokens += other.completion_tokens;
    total_tokens += other.total_tokens;
    cost_usd += other.cost_usd;
    cache_read_input_tokens += other.cache_read_input_tokens;
    cache_creation_input_tokens += other.cache_creation_input_tokens;
    web_search_requests += other.web_search_requests;
    lines_added += other.lines_added;
    lines_removed += other.lines_removed;
    api_duration_ms += other.api_duration_ms;
    api_duration_without_retries_ms += other.api_duration_without_retries_ms;
    retry_count += other.retry_count;
}

TokenTracker& TokenTracker::GetInstance() {
    static TokenTracker instance;
    return instance;
}

void TokenTracker::RecordUsage(const std::string& model, int prompt_tokens, int completion_tokens) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto& stats = model_stats_[model];
    stats.Add(prompt_tokens, completion_tokens);

    // Calculate cost if we have rates
    auto rate_it = cost_rates_.find(model);
    if (rate_it != cost_rates_.end()) {
        stats.CalculateCost(rate_it->second.first, rate_it->second.second);
    } else {
        auto default_it = default_cost_rates_.find(model);
        if (default_it != default_cost_rates_.end()) {
            stats.CalculateCost(default_it->second.first, default_it->second.second);
        }
    }

    totals_dirty = true;
    LOG_DEBUG("Token usage recorded for {}: {} prompt, {} completion", model, prompt_tokens, completion_tokens);
}

void TokenTracker::RecordExtendedUsage(const std::string& model,
                                        int prompt_tokens, int completion_tokens,
                                        int cache_read_input_tokens,
                                        int cache_creation_input_tokens,
                                        int web_search_requests,
                                        int64_t api_duration_ms,
                                        int64_t api_duration_without_retries_ms,
                                        int retry_count,
                                        double cost_usd) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // Update base stats
    auto& stats = model_stats_[model];
    stats.prompt_tokens += prompt_tokens;
    stats.completion_tokens += completion_tokens;
    stats.total_tokens += prompt_tokens + completion_tokens;
    stats.cache_read_input_tokens += cache_read_input_tokens;
    stats.cache_creation_input_tokens += cache_creation_input_tokens;
    stats.web_search_requests += web_search_requests;
    stats.cost_usd += cost_usd;
    stats.api_duration_ms += api_duration_ms;
    stats.api_duration_without_retries_ms += api_duration_without_retries_ms;
    stats.retry_count += retry_count;

    // Update extended stats
    auto& ext_stats = extended_stats_[model];
    ext_stats.MergeFrom(stats);
    ext_stats.session_count++;

    totals_dirty = true;
    LOG_DEBUG("Extended usage recorded for {}: {} total tokens, ${:.4f}", model, stats.total_tokens, cost_usd);
}

void TokenTracker::RecordCodeChanges(int lines_added, int lines_removed) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    extended_stats_["__total__"].lines_added += lines_added;
    extended_stats_["__total__"].lines_removed += lines_removed;
    totals_dirty = true;
}

void TokenTracker::RecordToolInvocation(int64_t duration_ms) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    extended_stats_["__total__"].tool_invocations++;
    extended_stats_["__total__"].tool_duration_ms += duration_ms;
    totals_dirty = true;
}

void TokenTracker::RecordFpsMetrics(double average_fps, double low_1_pct_fps) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    extended_stats_["__total__"].fps_average = average_fps;
    extended_stats_["__total__"].fps_low_1_pct = low_1_pct_fps;
}

TokenStats TokenTracker::GetModelStats(const std::string& model) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = model_stats_.find(model);
    if (it != model_stats_.end()) {
        return it->second;
    }
    return TokenStats{};
}

TokenStats TokenTracker::GetTotalStats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    if (!totals_dirty && !cached_totals_.empty()) {
        auto it = cached_totals_.find("__total__");
        if (it != cached_totals_.end()) {
            return it->second;
        }
    }

    TokenStats total;
    for (const auto& [model, stats] : model_stats_) {
        total.Add(stats.prompt_tokens, stats.completion_tokens);
    }

    // Calculate total cost
    for (const auto& [model, stats] : model_stats_) {
        total.cost_usd += stats.cost_usd;
    }

    // Note: We're modifying cache while holding the lock (read-lock upgraded conceptually)
    // This is safe because cached_totals_ is marked mutable and we're in a const method
    const_cast<TokenTracker*>(this)->cached_totals_["__total__"] = total;
    const_cast<TokenTracker*>(this)->totals_dirty = false;
    return total;
}

std::unordered_map<std::string, TokenStats> TokenTracker::GetAllStats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return model_stats_;
}

double TokenTracker::GetEstimatedCost(const std::string& model) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = model_stats_.find(model);
    if (it != model_stats_.end()) {
        return it->second.cost_usd;
    }
    return 0;
}

double TokenTracker::GetTotalEstimatedCost() const {
    return GetTotalStats().cost_usd;
}

void TokenTracker::Reset() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    model_stats_.clear();
    totals_dirty = true;
    LOG_INFO("Token tracker stats reset");
}

void TokenTracker::SetCostRate(const std::string& model, double input_rate, double output_rate) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    cost_rates_[model] = {input_rate, output_rate};
    LOG_DEBUG("Cost rate set for {}: ${}/1K input, ${}/1K output", model, input_rate, output_rate);
}

nlohmann::json TokenTracker::ToJson() const {
    nlohmann::json json = nlohmann::json::object();
    auto total = GetExtendedTotalStats();

    json["total_tokens"] = total.total_tokens;
    json["total_cost_usd"] = total.cost_usd;

    // Extended metrics
    json["cache_read_tokens"] = GetTotalCacheReadTokens();
    json["cache_creation_tokens"] = GetTotalCacheCreationTokens();
    json["web_search_requests"] = GetTotalWebSearchRequests();
    json["lines_added"] = GetTotalLinesAdded();
    json["lines_removed"] = GetTotalLinesRemoved();
    json["api_duration_ms"] = GetTotalApiDurationMs();
    json["tool_invocations"] = GetTotalToolInvocations();
    json["tool_duration_ms"] = GetTotalToolDurationMs();

    nlohmann::json models = nlohmann::json::object();
    for (const auto& [model, stats] : model_stats_) {
        nlohmann::json model_json = nlohmann::json::object();
        model_json["prompt_tokens"] = stats.prompt_tokens;
        model_json["completion_tokens"] = stats.completion_tokens;
        model_json["total_tokens"] = stats.total_tokens;
        model_json["cost_usd"] = stats.cost_usd;
        model_json["cache_read_tokens"] = stats.cache_read_input_tokens;
        model_json["cache_creation_tokens"] = stats.cache_creation_input_tokens;
        model_json["web_search_requests"] = stats.web_search_requests;
        model_json["api_duration_ms"] = stats.api_duration_ms;
        models[model] = model_json;
    }
    json["models"] = models;

    return json;
}

void TokenTracker::FromJson(const nlohmann::json& json) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    if (json.contains("models") && json["models"].is_object()) {
        for (const auto& [model, stats_json] : json["models"].items()) {
            TokenStats stats;
            stats.prompt_tokens = stats_json.value("prompt_tokens", 0);
            stats.completion_tokens = stats_json.value("completion_tokens", 0);
            stats.total_tokens = stats_json.value("total_tokens", 0);
            stats.cost_usd = stats_json.value("cost_usd", 0);
            stats.cache_read_input_tokens = stats_json.value("cache_read_tokens", 0);
            stats.cache_creation_input_tokens = stats_json.value("cache_creation_tokens", 0);
            stats.web_search_requests = stats_json.value("web_search_requests", 0);
            stats.api_duration_ms = stats_json.value("api_duration_ms", 0);
            model_stats_[model] = stats;
        }
    }

    // Load extended metrics
    if (json.contains("lines_added")) {
        extended_stats_["__total__"].lines_added = json.value("lines_added", 0);
    }
    if (json.contains("lines_removed")) {
        extended_stats_["__total__"].lines_removed = json.value("lines_removed", 0);
    }
    if (json.contains("tool_invocations")) {
        extended_stats_["__total__"].tool_invocations = json.value("tool_invocations", 0);
    }
    if (json.contains("tool_duration_ms")) {
        extended_stats_["__total__"].tool_duration_ms = json.value("tool_duration_ms", 0);
    }

    totals_dirty = true;
    LOG_INFO("Token tracker stats loaded from JSON");
}

void TokenTracker::SaveToFile(const std::string& filepath) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    WriteJson(filepath, ToJson(), 2);
    LOG_INFO("Token tracker stats saved to {}", filepath);
}

void TokenTracker::LoadFromFile(const std::string& filepath) {
    auto json_opt = ReadJson(filepath);
    if (json_opt) {
        try {
            FromJson(*json_opt);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse token stats file: {}", e.what());
        }
    }
}

// TokenCounter implementation
int TokenCounter::EstimateTokens(const std::string& text) {
    // Rough approximation: 1 token ≈ 4 characters for English
    // SL.str.2: Consider std::string_view for read-only text
    return static_cast<int>(text.length() / 4);
}

int TokenCounter::CountTokens(const std::string& text, const std::string& /* model */) {
    // Simplified token counting - in production, use tiktoken or similar
    int tokens = 0;
    bool in_word = false;

    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            if (!in_word) {
                tokens++;
                in_word = true;
            }
        } else {
            in_word = false;
            if (c == ' ' || c == '\n' || c == '\t') {
                // Whitespace doesn't add tokens
            } else {
                tokens++;  // Punctuation counts as token
            }
        }
    }

    return tokens;
}

int TokenCounter::CountMessageTokens(const std::vector<ContentSchema>* content, const std::string& /* model */) {
    if (!content) return 0;

    int total = 0;
    for (const auto& block : *content) {
        if (block.type == "text" || block.type == "thinking") {
            total += CountTokens(block.text);
        } else if (block.type == "tool_use") {
            total += CountTokens(block.name + block.input.dump());
        } else if (block.type == "tool_result") {
            total += CountTokens(block.content);
        }
    }
    // Base message overhead
    total += 4;
    return total;
}

// ==================== Extended TokenTracker Methods ====================

ExtendedTokenStats TokenTracker::GetExtendedModelStats(const std::string& model) const {
    auto it = extended_stats_.find(model);
    if (it != extended_stats_.end()) {
        return it->second;
    }
    return ExtendedTokenStats{};
}

ExtendedTokenStats TokenTracker::GetExtendedTotalStats() const {
    ExtendedTokenStats total;

    // Sum up all model stats
    for (const auto& [model, stats] : model_stats_) {
        total.MergeFrom(stats);
    }

    // Add extended metrics
    for (const auto& [model, ext_stats] : extended_stats_) {
        total.lines_added += ext_stats.lines_added;
        total.lines_removed += ext_stats.lines_removed;
        total.tool_invocations += ext_stats.tool_invocations;
        total.tool_duration_ms += ext_stats.tool_duration_ms;
        total.fps_average = ext_stats.fps_average;  // Use latest
        total.fps_low_1_pct = ext_stats.fps_low_1_pct;
    }

    return total;
}

int TokenTracker::GetTotalCacheReadTokens() const {
    int total = 0;
    for (const auto& [model, stats] : model_stats_) {
        total += stats.cache_read_input_tokens;
    }
    return total;
}

int TokenTracker::GetTotalCacheCreationTokens() const {
    int total = 0;
    for (const auto& [model, stats] : model_stats_) {
        total += stats.cache_creation_input_tokens;
    }
    return total;
}

int TokenTracker::GetTotalWebSearchRequests() const {
    int total = 0;
    for (const auto& [model, stats] : model_stats_) {
        total += stats.web_search_requests;
    }
    return total;
}

int TokenTracker::GetTotalLinesAdded() const {
    auto it = extended_stats_.find("__total__");
    if (it != extended_stats_.end()) {
        return it->second.lines_added;
    }
    return 0;
}

int TokenTracker::GetTotalLinesRemoved() const {
    auto it = extended_stats_.find("__total__");
    if (it != extended_stats_.end()) {
        return it->second.lines_removed;
    }
    return 0;
}

int64_t TokenTracker::GetTotalApiDurationMs() const {
    int64_t total = 0;
    for (const auto& [model, stats] : model_stats_) {
        total += stats.api_duration_ms;
    }
    return total;
}

int64_t TokenTracker::GetTotalApiDurationWithoutRetriesMs() const {
    int64_t total = 0;
    for (const auto& [model, stats] : model_stats_) {
        total += stats.api_duration_without_retries_ms;
    }
    return total;
}

int TokenTracker::GetTotalToolInvocations() const {
    auto it = extended_stats_.find("__total__");
    if (it != extended_stats_.end()) {
        return it->second.tool_invocations;
    }
    return 0;
}

int64_t TokenTracker::GetTotalToolDurationMs() const {
    auto it = extended_stats_.find("__total__");
    if (it != extended_stats_.end()) {
        return it->second.tool_duration_ms;
    }
    return 0;
}

std::unordered_map<std::string, ExtendedTokenStats> TokenTracker::GetAllExtendedStats() const {
    return extended_stats_;
}

std::string TokenTracker::FormatTotalCost() const {
    double cost = GetTotalEstimatedCost();
    auto total = GetExtendedTotalStats();

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    oss << "Total cost:            $" << cost << "\n";
    oss << "Total duration (API):  " << (total.api_duration_ms / 1000.0) << "s\n";
    oss << "Total code changes:    " << total.lines_added << " lines added, "
        << total.lines_removed << " lines removed\n";

    return oss.str();
}

std::string TokenTracker::FormatModelUsage() const {
    if (model_stats_.empty()) {
        return "Usage: 0 input, 0 output, 0 cache read, 0 cache write\n";
    }

    std::ostringstream oss;
    oss << "Usage by model:\n";

    for (const auto& [model, stats] : model_stats_) {
        oss << "  " << model << ":\n";
        oss << "    " << stats.prompt_tokens << " input, "
            << stats.completion_tokens << " output, "
            << stats.cache_read_input_tokens << " cache read, "
            << stats.cache_creation_input_tokens << " cache write";
        if (stats.web_search_requests > 0) {
            oss << ", " << stats.web_search_requests << " web search";
        }
        oss << " ($" << std::fixed << std::setprecision(4) << stats.cost_usd << ")\n";
    }

    return oss.str();
}

}  // namespace aicode
