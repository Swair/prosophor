// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ask_user_question_tool.h"

#include <iostream>
#include <sstream>

#include "common/log_wrapper.h"
#include "common/string_utils.h"

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace aicode {

AskUserQuestionTool& AskUserQuestionTool::GetInstance() {
    static AskUserQuestionTool instance;
    return instance;
}

std::vector<Question> AskUserQuestionTool::ParseQuestions(const nlohmann::json& json) {
    std::vector<Question> questions;

    for (const auto& q : json) {
        Question question;
        question.question = q.value("question", "");
        question.header = q.value("header", "");
        question.multiSelect = q.value("multiSelect", false);

        if (q.contains("options") && q["options"].is_array()) {
            for (const auto& opt : q["options"]) {
                QuestionOption option;
                option.label = opt.value("label", "");
                option.description = opt.value("description", "");
                option.preview = opt.value("preview", "");
                question.options.push_back(option);
            }
        }

        questions.push_back(question);
    }

    return questions;
}

bool AskUserQuestionTool::ValidateQuestions(const std::vector<Question>& questions,
                                             std::string& error) const {
    if (questions.empty() || questions.size() > 4) {
        error = "Must have 1-4 questions";
        return false;
    }

    // Check unique question texts
    std::unordered_map<std::string, bool> seen_questions;
    for (const auto& q : questions) {
        if (q.question.empty()) {
            error = "Question text cannot be empty";
            return false;
        }
        if (seen_questions.count(q.question)) {
            error = "Question texts must be unique";
            return false;
        }
        seen_questions[q.question] = true;

        // Check options
        if (q.options.size() < 2 || q.options.size() > 4) {
            error = "Each question must have 2-4 options";
            return false;
        }

        // Check unique option labels
        std::unordered_map<std::string, bool> seen_labels;
        for (const auto& opt : q.options) {
            if (opt.label.empty()) {
                error = "Option label cannot be empty";
                return false;
            }
            if (seen_labels.count(opt.label)) {
                error = "Option labels must be unique within each question";
                return false;
            }
            seen_labels[opt.label] = true;
        }

        // Check header length
        if (q.header.size() > 12) {
            error = "Header must be at most 12 characters";
            return false;
        }
    }

    return true;
}

std::string AskUserQuestionTool::FormatQuestions(const std::vector<Question>& questions) const {
    std::ostringstream oss;

    oss << "\n";
    for (size_t i = 0; i < questions.size(); ++i) {
        const auto& q = questions[i];

        // Header chip
        oss << "┌─ " << q.header << " ──────────────────────────────\n";
        oss << "│\n";
        oss << "│ " << (i + 1) << ". " << q.question << "\n";
        oss << "│\n";

        // Options
        for (size_t j = 0; j < q.options.size(); ++j) {
            const auto& opt = q.options[j];
            oss << "│   [" << static_cast<char>('A' + j) << "] " << opt.label << "\n";
            oss << "│       " << opt.description << "\n";
            if (!opt.preview.empty()) {
                oss << "│       Preview:\n";
                // Indent preview content
                std::istringstream preview_stream(opt.preview);
                std::string line;
                while (std::getline(preview_stream, line)) {
                    oss << "│       > " << line << "\n";
                }
            }
            oss << "│\n";
        }

        if (q.multiSelect) {
            oss << "│   (Select multiple: comma-separated, e.g., A,B,C)\n";
        }
        oss << "└──────────────────────────────────────────\n\n";
    }

    return oss.str();
}

char AskUserQuestionTool::ReadChar() const {
#ifdef _WIN32
    return _getch();
#else
    struct termios oldt, newt;
    char ch;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
#endif
}

std::string AskUserQuestionTool::ReadLine() const {
    return aicode::ReadLine();
}

AskUserQuestionResult AskUserQuestionTool::Ask(const std::vector<Question>& questions) {
    AskUserQuestionResult result;
    result.questions = questions;

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════╗\n";
    std::cout << "║         CLAUDE HAS A QUESTION              ║\n";
    std::cout << "╚════════════════════════════════════════════╝\n\n";

    std::cout << FormatQuestions(questions);

    // Collect answers
    for (const auto& q : questions) {
        while (true) {
            std::cout << "Your answer [";
            for (size_t j = 0; j < q.options.size(); ++j) {
                std::cout << static_cast<char>('A' + j);
                if (j < q.options.size() - 1) std::cout << "/";
            }
            std::cout << "]: ";

            std::string answer = ReadLine();

            // Validate answer
            bool valid = true;
            if (answer.empty()) {
                valid = false;
            } else if (q.multiSelect) {
                // Validate comma-separated answers
                std::istringstream iss(answer);
                std::string part;
                while (std::getline(iss, part, ',')) {
                    // Trim whitespace
                    size_t start = part.find_first_not_of(" \t");
                    size_t end = part.find_last_not_of(" \t");
                    if (start != std::string::npos && end != std::string::npos) {
                        part = part.substr(start, end - start + 1);
                    }
                    if (part.empty() || part.size() != 1 ||
                        toupper(part[0]) < 'A' ||
                        toupper(part[0]) >= 'A' + static_cast<int>(q.options.size())) {
                        valid = false;
                        break;
                    }
                }
            } else {
                // Single select
                if (answer.size() != 1 ||
                    toupper(answer[0]) < 'A' ||
                    toupper(answer[0]) >= 'A' + static_cast<int>(q.options.size())) {
                    valid = false;
                }
            }

            if (!valid) {
                std::cout << "Invalid input. Please try again.\n";
                continue;
            }

            // Convert to option labels
            std::string answer_text;
            if (q.multiSelect) {
                std::vector<std::string> selected;
                std::istringstream iss(answer);
                std::string part;
                while (std::getline(iss, part, ',')) {
                    size_t start = part.find_first_not_of(" \t");
                    if (start != std::string::npos) {
                        int idx = toupper(part[start]) - 'A';
                        selected.push_back(q.options[idx].label);
                    }
                }
                for (size_t k = 0; k < selected.size(); ++k) {
                    if (k > 0) answer_text += ", ";
                    answer_text += selected[k];
                }
            } else {
                int idx = toupper(answer[0]) - 'A';
                answer_text = q.options[idx].label;
            }

            result.answers[q.question] = answer_text;
            std::cout << "Answer recorded: " << answer_text << "\n\n";
            break;
        }
    }

    std::cout << "════════════════════════════════════════════\n";
    std::cout << "All answers recorded. Continuing...\n";
    std::cout << "════════════════════════════════════════════\n\n";

    return result;
}

}  // namespace aicode
