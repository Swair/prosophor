// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "managers/permission_manager.h"

#include <algorithm>
#include <regex>
#include <fstream>
#include <sstream>

#include "common/log_wrapper.h"

namespace prosophor {

// Simple glob-style pattern matching
// Supports: * (any sequence), ? (single char), case-insensitive
static bool PatternMatches(const std::string& pattern, const std::string& value) {
    if (pattern.empty()) return true;

    // Convert glob pattern to regex
    std::string regex_pattern;
    for (char c : pattern) {
        switch (c) {
            case '*': regex_pattern += ".*"; break;
            case '?': regex_pattern += "."; break;
            case '.': regex_pattern += "\\."; break;
            case '+': regex_pattern += "\\+"; break;
            case '[': regex_pattern += "\\["; break;
            case ']': regex_pattern += "\\]"; break;
            case '(': regex_pattern += "\\("; break;
            case ')': regex_pattern += "\\)"; break;
            case '^': regex_pattern += "\\^"; break;
            case '$': regex_pattern += "\\$"; break;
            default: regex_pattern += c; break;
        }
    }
    regex_pattern = "^" + regex_pattern + "$";

    try {
        std::regex re(regex_pattern, std::regex::icase);
        return std::regex_match(value, re);
    } catch (const std::regex_error&) {
        // Fall back to exact match if regex compilation fails
        return pattern == value;
    }
}

bool PermissionRule::MatchesTool(const std::string& tool_name) const {
    return PatternMatches(tool_name, tool_name);
}

bool PermissionRule::MatchesCommand(const std::string& command) const {
    return PatternMatches(command_pattern, command);
}

bool PermissionRule::MatchesPath(const std::string& path) const {
    return PatternMatches(path_pattern, path);
}

bool PermissionRule::MatchesArg(const std::string& arg_value) const {
    // Check denied args first
    for (const auto& denied : denied_args) {
        if (PatternMatches(denied, arg_value)) {
            return false;
        }
    }
    // Check allowed args
    if (allowed_args.empty()) {
        return true;  // No restrictions
    }
    for (const auto& allowed : allowed_args) {
        if (PatternMatches(allowed, arg_value)) {
            return true;
        }
    }
    return false;
}

PermissionManager& PermissionManager::GetInstance() {
    static PermissionManager instance;
    return instance;
}

void PermissionManager::Initialize(const nlohmann::json& config) {
    ClearRules();

    if (config.contains("mode")) {
        mode_ = config.value("mode", "default");
    }

    // Load allow rules
    if (config.contains("allow")) {
        for (const auto& rule_config : config["allow"]) {
            PermissionRule rule;
            rule.tool_name = rule_config.value("tool", "*");
            rule.command_pattern = rule_config.value("command", "");
            rule.path_pattern = rule_config.value("path", "");
            rule.default_level = PermissionLevel::Allow;
            if (rule_config.contains("allowed_args")) {
                rule.allowed_args = rule_config["allowed_args"].get<std::vector<std::string>>();
            }
            if (rule_config.contains("denied_args")) {
                rule.denied_args = rule_config["denied_args"].get<std::vector<std::string>>();
            }
            allow_rules_.push_back(rule);
        }
    }

    // Load deny rules
    if (config.contains("deny")) {
        for (const auto& rule_config : config["deny"]) {
            PermissionRule rule;
            rule.tool_name = rule_config.value("tool", "*");
            rule.command_pattern = rule_config.value("command", "");
            rule.path_pattern = rule_config.value("path", "");
            rule.default_level = PermissionLevel::Deny;
            deny_rules_.push_back(rule);
        }
    }

    // Load ask rules
    if (config.contains("ask")) {
        for (const auto& rule_config : config["ask"]) {
            PermissionRule rule;
            rule.tool_name = rule_config.value("tool", "*");
            rule.command_pattern = rule_config.value("command", "");
            rule.path_pattern = rule_config.value("path", "");
            rule.default_level = PermissionLevel::Ask;
            ask_rules_.push_back(rule);
        }
    }

    LOG_DEBUG("PermissionManager initialized with mode={}, allow_rules={}, deny_rules={}, ask_rules={}",
             mode_, allow_rules_.size(), deny_rules_.size(), ask_rules_.size());
}

const PermissionRule* PermissionManager::FindMatchingRule(
    const std::vector<PermissionRule>& rules,
    const std::string& tool_name,
    const nlohmann::json& input) const {

    for (const auto& rule : rules) {
        if (!rule.MatchesTool(tool_name)) {
            continue;
        }

        std::string command = ExtractCommand(tool_name, input);
        if (!rule.command_pattern.empty() && !rule.MatchesCommand(command)) {
            continue;
        }

        std::string path = ExtractPath(tool_name, input);
        if (!rule.path_pattern.empty() && !rule.MatchesPath(path)) {
            continue;
        }

        return &rule;
    }

    return nullptr;
}

std::string PermissionManager::ExtractPath(const std::string& tool_name,
                                           const nlohmann::json& input) const {
    // Common path parameter names
    static const std::vector<std::string> path_keys = {"path", "file", "filepath", "filename"};

    for (const auto& key : path_keys) {
        if (input.contains(key) && input[key].is_string()) {
            return input[key].get<std::string>();
        }
    }

    return "";
}

std::string PermissionManager::ExtractCommand(const std::string& tool_name,
                                               const nlohmann::json& input) const {
    if (tool_name == "bash" || tool_name == "exec") {
        if (input.contains("command") && input["command"].is_string()) {
            return input["command"].get<std::string>();
        }
    }
    return "";
}

PermissionResult PermissionManager::CheckPermission(
    const std::string& tool_name,
    const nlohmann::json& input) {

    std::lock_guard<std::mutex> lock(mutex_);

    // Bypass mode - allow everything
    if (mode_ == "bypass") {
        return PermissionResult::Allow();
    }

    // Auto mode for non-interactive sessions
    if (mode_ == "auto") {
        // In auto mode, deny dangerous operations
        if (tool_name == "bash") {
            std::string command = ExtractCommand(tool_name, input);
            // Auto-deny destructive commands
            static const std::vector<std::string> dangerous_cmds = {
                "rm -rf", "sudo rm", "mkfs", "dd if=", ":(){:|:&}", "chmod -R 777"
            };
            for (const auto& dangerous : dangerous_cmds) {
                if (command.find(dangerous) != std::string::npos) {
                    return PermissionResult::Deny("Dangerous command detected: " + dangerous);
                }
            }
        }
        return PermissionResult::Allow();
    }

    // Check deny rules first (highest priority)
    const PermissionRule* deny_rule = FindMatchingRule(deny_rules_, tool_name, input);
    if (deny_rule) {
        LOG_DEBUG("Permission DENIED by rule: tool={}, command={}, path={}",
                  tool_name, ExtractCommand(tool_name, input), ExtractPath(tool_name, input));
        RecordDenial(tool_name, input);
        return PermissionResult::Deny("Matched deny rule");
    }

    // Check allow rules
    const PermissionRule* allow_rule = FindMatchingRule(allow_rules_, tool_name, input);
    if (allow_rule) {
        LOG_DEBUG("Permission ALLOWED by rule: tool={}", tool_name);
        return PermissionResult::Allow();
    }

    // Check ask rules
    const PermissionRule* ask_rule = FindMatchingRule(ask_rules_, tool_name, input);
    if (ask_rule) {
        return PermissionResult::Ask();
    }

    // Check fallback to allow due to repeated denials
    if (ShouldFallbackToAllow(tool_name)) {
        LOG_INFO("Fallback to ALLOW for tool={} after {} denials",
                 tool_name, GetDenialCount(tool_name));
        return PermissionResult::Allow();
    }

    // Default: ask for confirmation
    return PermissionResult::Ask();
}

void PermissionManager::AddAllowRule(const PermissionRule& rule) {
    allow_rules_.push_back(rule);
    LOG_DEBUG("Added ALLOW rule for tool={}", rule.tool_name);
}

void PermissionManager::AddDenyRule(const PermissionRule& rule) {
    deny_rules_.push_back(rule);
    LOG_DEBUG("Added DENY rule for tool={}", rule.tool_name);
}

void PermissionManager::AddAskRule(const PermissionRule& rule) {
    ask_rules_.push_back(rule);
    LOG_DEBUG("Added ASK rule for tool={}", rule.tool_name);
}

void PermissionManager::ClearRules() {
    allow_rules_.clear();
    deny_rules_.clear();
    ask_rules_.clear();
    denial_counts_.clear();
    LOG_DEBUG("Permission rules cleared");
}

void PermissionManager::RecordDenial(const std::string& tool_name, const nlohmann::json& /*input*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    denial_counts_[tool_name]++;
    LOG_DEBUG("Recorded denial for tool={}, count={}", tool_name, denial_counts_[tool_name]);
}

int PermissionManager::GetDenialCount(const std::string& tool_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = denial_counts_.find(tool_name);
    return (it != denial_counts_.end()) ? it->second : 0;
}

bool PermissionManager::ShouldFallbackToAllow(const std::string& tool_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = denial_counts_.find(tool_name);
    return (it != denial_counts_.end() && it->second >= kFallbackThreshold);
}

bool PermissionManager::RequestUserConfirmation(
    const std::string& tool_name,
    const nlohmann::json& input,
    const std::string& reason) {

    if (!confirm_callback_) {
        // No callback set - default to deny in non-interactive mode
        LOG_WARN("No confirmation callback set, defaulting to DENY for tool={}", tool_name);
        return false;
    }

    return confirm_callback_(tool_name, input, reason);
}

void PermissionManager::SetMode(const std::string& mode) {
    mode_ = mode;
    LOG_DEBUG("Permission mode changed to: {}", mode);
}

}  // namespace prosophor
