// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace aicode {

/// Question option for AskUserQuestionTool
struct QuestionOption {
    std::string label;        // Display text (1-5 words)
    std::string description;  // Explanation of what this option means
    std::string preview;      // Optional preview content (code snippet, ASCII art, etc.)
};

/// Single question definition
struct Question {
    std::string question;     // The complete question to ask
    std::string header;       // Short label displayed as chip (max 12 chars)
    std::vector<QuestionOption> options;  // 2-4 options
    bool multiSelect = false;  // Allow multiple selections
};

/// AskUserQuestionTool result
struct AskUserQuestionResult {
    std::vector<Question> questions;
    std::unordered_map<std::string, std::string> answers;  // question -> answer
    std::unordered_map<std::string, std::string> annotations;  // question -> notes
};

/// Ask user question tool implementation
class AskUserQuestionTool {
public:
    static AskUserQuestionTool& GetInstance();

    /// Execute the tool and collect user answers
    /// @param questions List of questions to ask (1-4)
    /// @return Result with user answers
    AskUserQuestionResult Ask(const std::vector<Question>& questions);

    /// Parse questions from JSON
    std::vector<Question> ParseQuestions(const nlohmann::json& json);

    /// Format questions for display
    std::string FormatQuestions(const std::vector<Question>& questions) const;

    /// Validate questions
    bool ValidateQuestions(const std::vector<Question>& questions, std::string& error) const;

private:
    AskUserQuestionTool() = default;

    /// Read single character input
    char ReadChar() const;

    /// Read line input
    std::string ReadLine() const;
};

}  // namespace aicode
