// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>

namespace aicode {

/// Permission level for tool execution
enum class PermissionLevel {
    Allow,      // Auto-approve
    Deny,       // Auto-deny
    Ask         // Require user confirmation
};

/// Permission result for a tool call
struct PermissionResult {
    PermissionLevel level = PermissionLevel::Ask;
    std::string reason;
    nlohmann::json updated_input;  // Modified input after permission check

    static PermissionResult Allow() {
        return {PermissionLevel::Allow, "Auto-approved", nlohmann::json::object()};
    }

    static PermissionResult Deny(const std::string& reason = "Access denied") {
        return {PermissionLevel::Deny, reason, nlohmann::json::object()};
    }

    static PermissionResult Ask() {
        return {PermissionLevel::Ask, "User confirmation required", nlohmann::json::object()};
    }
};

/// Permission rule for matching tool calls
struct PermissionRule {
    std::string tool_name;      // Tool name pattern (e.g., "bash", "read_*")
    std::string command_pattern; // For bash tool: command pattern (e.g., "git *", "npm test")
    std::string path_pattern;   // For file tools: path pattern
    PermissionLevel default_level = PermissionLevel::Ask;
    std::vector<std::string> allowed_args;  // Allowed argument values
    std::vector<std::string> denied_args;   // Denied argument values

    bool MatchesTool(const std::string& tool_name) const;
    bool MatchesCommand(const std::string& command) const;
    bool MatchesPath(const std::string& path) const;
    bool MatchesArg(const std::string& arg_value) const;
};

/// Permission manager for tool call authorization
class PermissionManager {
public:
    static PermissionManager& GetInstance();

    /// Initialize permission manager with configuration
    void Initialize(const nlohmann::json& config = nlohmann::json::object());

    /// Set permission mode
    void SetMode(const std::string& mode);  // "auto", "default", "bypass"

    /// Get current mode
    const std::string& GetMode() const { return mode_; }

    /// Check permission for a tool call
    PermissionResult CheckPermission(
        const std::string& tool_name,
        const nlohmann::json& input);

    /// Add an always-allow rule
    void AddAllowRule(const PermissionRule& rule);

    /// Add an always-deny rule
    void AddDenyRule(const PermissionRule& rule);

    /// Add an always-ask rule
    void AddAskRule(const PermissionRule& rule);

    /// Clear all rules
    void ClearRules();

    /// Get all rules (for debugging)
    std::vector<PermissionRule> GetAllowRules() const { return allow_rules_; }
    std::vector<PermissionRule> GetDenyRules() const { return deny_rules_; }
    std::vector<PermissionRule> GetAskRules() const { return ask_rules_; }

    /// Record a denial for tracking (used for fallback logic)
    void RecordDenial(const std::string& tool_name, const nlohmann::json& input);

    /// Get denial count for a tool
    int GetDenialCount(const std::string& tool_name) const;

    /// Check if a tool should be auto-approved due to repeated denials
    bool ShouldFallbackToAllow(const std::string& tool_name) const;

    /// Set user confirmation callback (for interactive mode)
    using ConfirmCallback = std::function<bool(const std::string& tool_name,
                                                const nlohmann::json& input,
                                                const std::string& reason)>;
    void SetConfirmCallback(ConfirmCallback cb) { confirm_callback_ = std::move(cb); }

    /// Execute confirmation callback if set
    bool RequestUserConfirmation(const std::string& tool_name,
                                  const nlohmann::json& input,
                                  const std::string& reason);

private:
    PermissionManager() = default;
    ~PermissionManager() = default;

    mutable std::mutex mutex_;  // Protects denial_counts_ and mode_
    std::string mode_ = "default";
    std::vector<PermissionRule> allow_rules_;
    std::vector<PermissionRule> deny_rules_;
    std::vector<PermissionRule> ask_rules_;

    // Denial tracking for fallback logic
    std::unordered_map<std::string, int> denial_counts_;
    static constexpr int kFallbackThreshold = 3;  // Auto-allow after N denials

    ConfirmCallback confirm_callback_;

    /// Find matching rule from a list
    const PermissionRule* FindMatchingRule(
        const std::vector<PermissionRule>& rules,
        const std::string& tool_name,
        const nlohmann::json& input) const;

    /// Extract path from tool input
    std::string ExtractPath(const std::string& tool_name,
                            const nlohmann::json& input) const;

    /// Extract command from tool input
    std::string ExtractCommand(const std::string& tool_name,
                                const nlohmann::json& input) const;
};

}  // namespace aicode
