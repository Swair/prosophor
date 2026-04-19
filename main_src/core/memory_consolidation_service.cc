// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "core/memory_consolidation_service.h"

#include <sstream>
#include <regex>
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <memory>

#include "common/log_wrapper.h"
#include "common/time_wrapper.h"
#include "common/file_utils.h"

namespace aicode {

MemoryConsolidationService& MemoryConsolidationService::GetInstance() {
    static MemoryConsolidationService instance;
    return instance;
}

ConsolidationResult MemoryConsolidationService::ConsolidateSegmentAndSave(
    const AgentSession& session,
    int start_index,
    int end_index,
    LlmSummaryCallback llm_callback) {

    // Set callback temporarily
    auto old_callback = llm_callback_;
    llm_callback_ = llm_callback;

    // Consolidate segment
    auto result = ConsolidateSegment(session.messages, start_index, end_index);

    // Save to Role Memory if has result
    if (!result.summary.empty() && session.role) {
        AppendToRoleMemory(session, result, "segments");
    }

    // Restore old callback (if any)
    llm_callback_ = old_callback;

    return result;
}

bool MemoryConsolidationService::NeedsLengthConsolidation(
    const std::vector<MessageSchema>& messages,
    int threshold) const {

    if (!auto_consolidation_enabled_) {
        return false;
    }

    return static_cast<int>(messages.size()) >= threshold;
}

ConsolidationResult MemoryConsolidationService::ConsolidateSegment(
    const std::vector<MessageSchema>& messages,
    int start_index,
    int end_index) {

    ConsolidationResult result;

    if (start_index >= end_index || start_index >= static_cast<int>(messages.size())) {
        LOG_WARN("Invalid segment range: [{}, {})", start_index, end_index);
        return result;
    }

    // Extract segment
    std::vector<MessageSchema> segment;
    int actual_end = std::min(end_index, static_cast<int>(messages.size()));
    for (int i = start_index; i < actual_end; ++i) {
        segment.push_back(messages[i]);
    }

    result.messages_processed = static_cast<int>(segment.size());

    if (segment.empty()) {
        LOG_DEBUG("Empty segment, skipping consolidation");
        return result;
    }

    // Extract key decisions
    result.decisions = ExtractKeyDecisions(segment);

    // Generate summary
    if (llm_callback_) {
        try {
            std::string prompt = BuildSessionSummaryPrompt(segment);
            result.summary = llm_callback_(prompt);
            LOG_INFO("Generated summary for segment [{}, {})", start_index, actual_end);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to generate segment summary: {}", e.what());
            // Fallback: simple text summary
            std::ostringstream fallback;
            fallback << "[Segment " << start_index << "-" << actual_end
                     << " with " << segment.size() << " messages]";
            result.summary = fallback.str();
        }
    } else {
        // Fallback without LLM
        std::ostringstream fallback;
        fallback << "[Segment " << start_index << "-" << actual_end
                 << " with " << segment.size() << " messages]";
        result.summary = fallback.str();
    }

    // Estimate tokens
    int total_chars = 0;
    for (const auto& msg : segment) {
        for (const auto& block : msg.content) {
            if (block.type == "text" || block.type == "thinking") {
                total_chars += block.text.size();
            }
        }
    }
    result.tokens_estimated = total_chars / 4;  // Rough estimate

    LOG_INFO("Segment consolidation complete: {} messages, ~{} tokens, {} decisions",
             result.messages_processed, result.tokens_estimated, result.decisions.size());

    return result;
}

ConsolidationResult MemoryConsolidationService::ConsolidateSessionExit(
    const AgentSession& session,
    LlmSummaryCallback llm_callback) {

    ConsolidationResult result;

    if (session.messages.empty()) {
        LOG_DEBUG("Empty session, skipping exit consolidation");
        return result;
    }

    LOG_INFO("Consolidating session exit: {} messages", session.messages.size());

    result.messages_processed = static_cast<int>(session.messages.size());

    // Set callback temporarily for this call
    auto old_callback = llm_callback_;
    llm_callback_ = llm_callback;

    // Extract key decisions from entire session
    result.decisions = ExtractKeyDecisions(session.messages);

    // Generate session summary
    if (llm_callback_) {
        try {
            std::string prompt = BuildSessionSummaryPrompt(session.messages);
            result.summary = llm_callback_(prompt);
            LOG_INFO("Generated session exit summary");
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to generate session exit summary: {}", e.what());
            std::ostringstream fallback;
            fallback << "[Session " << session.session_id
                     << " with " << session.messages.size() << " messages]";
            result.summary = fallback.str();
        }
    }

    // Restore old callback
    llm_callback_ = old_callback;

    // Estimate tokens
    int total_chars = 0;
    for (const auto& msg : session.messages) {
        for (const auto& block : msg.content) {
            if (block.type == "text" || block.type == "thinking") {
                total_chars += block.text.size();
            }
        }
    }
    result.tokens_estimated = total_chars / 4;

    // Save to Role Memory
    if (session.role && !session.role->memory_dir.empty()) {
        AppendToRoleMemory(session, result, "exit_summary");
    }

    LOG_INFO("Session exit consolidation complete: ~{} tokens, {} decisions",
             result.tokens_estimated, result.decisions.size());

    return result;
}

ConsolidationResult MemoryConsolidationService::ConsolidateSessionExit(
    const AgentSession& session) {
    // Use internal callback if not provided
    return ConsolidateSessionExit(session, llm_callback_);
}

std::vector<KeyDecision> MemoryConsolidationService::ExtractKeyDecisions(
    const std::vector<MessageSchema>& messages) {

    std::vector<KeyDecision> decisions;

    if (!llm_callback_) {
        LOG_DEBUG("No LLM callback, skipping key decision extraction");
        return decisions;
    }

    try {
        std::string prompt = BuildExtractionPrompt(messages);
        std::string response = llm_callback_(prompt);
        decisions = ParseDecisionsFromResponse(response);
        LOG_INFO("Extracted {} key decisions", decisions.size());
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to extract key decisions: {}", e.what());
    }

    return decisions;
}

std::string MemoryConsolidationService::BuildExtractionPrompt(
    const std::vector<MessageSchema>& messages) {

    std::ostringstream prompt;
    prompt << "You are a memory consolidation assistant. Analyze the conversation below "
           << "and extract key decisions, code changes, and lessons learned.\n\n"
           << "For each key point, identify:\n"
           << "1. **Type**: design_decision | code_change | unresolved_issue | lesson_learned\n"
           << "2. **Content**: Brief description of what was decided/changed\n"
           << "3. **Related Files**: Any files mentioned or modified\n\n"
           << "Format your response as:\n"
           << "```\n"
           << "TYPE: <type>\n"
           << "CONTENT: <content>\n"
           << "FILES: <file1>, <file2>\n"
           << "```\n\n"
           << "Separate multiple decisions with `---`\n\n"
           << "Conversation:\n";

    // Add conversation transcript (limit to recent 50 messages for context)
    int start = std::max(0, static_cast<int>(messages.size()) - 50);
    for (size_t i = start; i < messages.size(); ++i) {
        const auto& msg = messages[i];
        std::string role = (msg.role == "user") ? "User" :
                           (msg.role == "assistant") ? "Assistant" : msg.role;

        for (const auto& block : msg.content) {
            if (block.type == "text" || block.type == "thinking") {
                // Truncate long text
                std::string text = block.text;
                if (text.size() > 500) {
                    text = text.substr(0, 500) + "...";
                }
                prompt << role << ": " << text << "\n\n";
            } else if (block.type == "tool_use") {
                prompt << role << " [Using tool: " << block.name << "]\n";
            } else if (block.type == "tool_result") {
                prompt << "[Tool result]\n\n";
            }
        }
    }

    return prompt.str();
}

std::string MemoryConsolidationService::BuildSessionSummaryPrompt(
    const std::vector<MessageSchema>& messages) {

    std::ostringstream prompt;
    prompt << "Please provide a concise summary of this conversation session.\n"
           << "The summary will be stored as long-term memory for future reference.\n\n"
           << "Include:\n"
           << "1. Key decisions and conclusions reached\n"
           << "2. Code changes that were made or discussed\n"
           << "3. Files that were modified\n"
           << "4. Any unresolved issues or TODOs\n"
           << "5. Important context about the task or codebase\n\n"
           << "Keep the summary concise but comprehensive (200-500 words).\n\n"
           << "Conversation:\n";

    // Add conversation transcript (limit to recent 50 messages)
    int start = std::max(0, static_cast<int>(messages.size()) - 50);
    for (size_t i = start; i < messages.size(); ++i) {
        const auto& msg = messages[i];
        std::string role = (msg.role == "user") ? "User" :
                           (msg.role == "assistant") ? "Assistant" : msg.role;

        for (const auto& block : msg.content) {
            if (block.type == "text" || block.type == "thinking") {
                std::string text = block.text;
                if (text.size() > 500) {
                    text = text.substr(0, 500) + "...";
                }
                prompt << role << ": " << text << "\n\n";
            }
        }
    }

    return prompt.str();
}

std::vector<KeyDecision> MemoryConsolidationService::ParseDecisionsFromResponse(
    const std::string& response) {

    std::vector<KeyDecision> decisions;

    // Split by ---
    std::regex separator_regex(R"(^---\s*$)", std::regex_constants::multiline);
    std::sregex_token_iterator it(response.begin(), response.end(), separator_regex, -1);
    std::sregex_token_iterator end;

    std::vector<std::string> segments;
    while (it != end) {
        segments.push_back(*it++);
    }

    // Parse each segment - E.1: Handle regex compilation errors
    std::regex type_regex(R"(TYPE:\s*(\S+))", std::regex_constants::icase);
    std::regex content_regex(R"(CONTENT:\s*(.+?)(?=FILES:|$))",
                             std::regex_constants::icase);
    std::regex files_regex(R"(FILES:\s*(.+?)(?=TYPE:|$))",
                           std::regex_constants::icase);

    for (const auto& segment : segments) {
        if (segment.find_first_not_of(" \t\n\r") == std::string::npos) {
            continue;  // Skip empty segments
        }

        KeyDecision decision;
        decision.timestamp = SystemClock::GetCurrentTimestamp();

        // Extract type
        std::smatch type_match;
        if (std::regex_search(segment, type_match, type_regex) && type_match.size() > 1) {
            decision.type = type_match[1].str();
        } else {
            decision.type = "lesson_learned";  // Default type
        }

        // Extract content
        std::smatch content_match;
        if (std::regex_search(segment, content_match, content_regex) && content_match.size() > 1) {
            decision.content = content_match[1].str();
            // Trim whitespace
            decision.content = std::regex_replace(decision.content, std::regex("^\\s+|\\s+$"), "");
        }

        // Extract files
        std::smatch files_match;
        if (std::regex_search(segment, files_match, files_regex) && files_match.size() > 1) {
            std::string files_str = files_match[1].str();
            // Split by comma
            std::istringstream file_stream(files_str);
            std::string file;
            while (std::getline(file_stream, file, ',')) {
                // Trim whitespace
                file = std::regex_replace(file, std::regex("^\\s+|\\s+$"), "");
                if (!file.empty()) {
                    decision.related_files.push_back(file);
                }
            }
        }

        if (!decision.content.empty()) {
            decisions.push_back(decision);
        }
    }

    return decisions;
}

void MemoryConsolidationService::AppendToRoleMemory(
    const AgentSession& session,
    const ConsolidationResult& result,
    const std::string& category) {

    if (!session.role || session.role->memory_dir.empty()) {
        LOG_WARN("No role memory directory available");
        return;
    }

    std::string date_str = SystemClock::GetCurrentDate();
    std::string timestamp_str = SystemClock::GetCurrentTimestamp();

    // Create category subdirectory
    auto memory_dir = std::filesystem::path(session.role->memory_dir) / category;
    std::filesystem::create_directories(memory_dir);

    // Build memory entry
    std::ostringstream entry_stream;
    entry_stream << "## " << timestamp_str << "\n\n";

    if (!result.summary.empty()) {
        entry_stream << "### Summary\n\n" << result.summary << "\n\n";
    }

    if (!result.decisions.empty()) {
        entry_stream << "### Key Decisions\n\n";
        for (const auto& decision : result.decisions) {
            entry_stream << "- **[" << decision.type << "]** " << decision.content;
            if (!decision.related_files.empty()) {
                entry_stream << " (Files: ";
                for (size_t i = 0; i < decision.related_files.size(); ++i) {
                    if (i > 0) entry_stream << ", ";
                    entry_stream << decision.related_files[i];
                }
                entry_stream << ")\n";
            } else {
                entry_stream << "\n";
            }
        }
        entry_stream << "\n";
    }

    // Save to daily file - with thread-safe locking
    auto memory_file = memory_dir / (date_str + ".md");
    auto lock = GetFileLock(memory_file.string());

    if (!WriteFile(memory_file.string(), entry_stream.str(), true)) {
        LOG_ERROR("Failed to write to role memory: {}", memory_file.string());
    } else {
        LOG_INFO("Appended consolidation to role memory: {}", memory_file.string());
    }
}

void MemoryConsolidationService::SaveDailyMemory(
    const std::string& role_memory_dir,
    const std::string& content,
    const std::string& date_str) {

    std::string actual_date = date_str.empty() ? SystemClock::GetCurrentDate() : date_str;
    std::string timestamp_str = SystemClock::GetCurrentTimestamp();

    auto memory_dir = std::filesystem::path(role_memory_dir) / "consolidation";
    std::filesystem::create_directories(memory_dir);

    std::ostringstream entry_stream;
    entry_stream << "## " << timestamp_str << "\n\n" << content << "\n";

    // Save with thread-safe locking
    auto memory_file = memory_dir / (actual_date + ".md");
    auto lock = GetFileLock(memory_file.string());

    if (!WriteFile(memory_file.string(), entry_stream.str(), true)) {
        LOG_ERROR("Failed to write daily memory: {}", memory_file.string());
    } else {
        LOG_DEBUG("Saved daily memory: {}", memory_file.string());
    }
}

std::unique_lock<std::mutex> MemoryConsolidationService::GetFileLock(
    const std::string& file_path) {

    std::lock_guard<std::mutex> global_lock(file_write_mutex_);

    // Find or create mutex for this file path
    auto it = file_mutexes_.find(file_path);
    if (it == file_mutexes_.end()) {
        // Create new mutex for this file
        file_mutexes_.emplace(file_path, std::make_unique<std::mutex>());
        it = file_mutexes_.find(file_path);
    }

    // Return locked guard for this file's mutex
    return std::unique_lock<std::mutex>(*it->second);
}

}  // namespace aicode
