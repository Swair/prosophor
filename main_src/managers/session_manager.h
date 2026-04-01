// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include "core/messages_schema.h"

namespace aicode {

/// Session metadata
struct SessionInfo {
    std::string session_id;
    std::string created_at;
    std::string updated_at;
    std::string workspace;
    int message_count = 0;
    int token_count = 0;
    double cost_usd = 0;
    std::string last_user_message;
};

/// Session manager for saving and restoring conversations
class SessionManager {
public:
    static SessionManager& GetInstance();

    /// Initialize session manager
    void Initialize(const std::string& sessions_dir = "~/.aicode/sessions");

    /// Start a new session
    std::string StartSession(const std::string& workspace = "");

    /// Get current session ID
    const std::string& GetCurrentSessionId() const { return current_session_id_; }

    /// Save current session
    void SaveCurrentSession();

    /// Save session to file
    void SaveSession(const std::string& session_id,
                     const std::vector<MessageSchema>& messages,
                     const nlohmann::json& metadata = nlohmann::json::object());

    /// Load session from file
    bool LoadSession(const std::string& session_id,
                     std::vector<MessageSchema>& messages,
                     nlohmann::json& metadata);

    /// List all sessions
    std::vector<SessionInfo> ListSessions() const;

    /// Get session info
    SessionInfo GetSessionInfo(const std::string& session_id) const;

    /// Delete a session
    bool DeleteSession(const std::string& session_id);

    /// Get the most recent session
    std::string GetLastSessionId() const;

    /// Resume last session
    bool ResumeLastSession(std::vector<MessageSchema>& messages, nlohmann::json& metadata);

    /// Add message to current session history (for auto-save)
    void AddMessageToSession(const MessageSchema& msg);

    /// Update session metadata
    void UpdateSessionMetadata(const nlohmann::json& metadata);

    /// Export session to JSON
    nlohmann::json SessionToJson(const std::vector<MessageSchema>& messages,
                                  const nlohmann::json& metadata) const;

    /// Import session from JSON
    void SessionFromJson(const nlohmann::json& json,
                         std::vector<MessageSchema>& messages,
                         nlohmann::json& metadata) const;

private:
    SessionManager() = default;
    ~SessionManager() = default;

    std::string sessions_dir_;
    std::string current_session_id_;
    std::vector<MessageSchema> current_messages_;
    nlohmann::json current_metadata_;

    /// Generate unique session ID
    std::string GenerateSessionId();

    /// Get session file path
    std::string GetSessionPath(const std::string& session_id) const;

    /// Get current timestamp string
    std::string GetTimestamp() const;

    /// Count tokens in messages
    int CountTokens(const std::vector<MessageSchema>& messages) const;

    /// Ensure sessions directory exists
    void EnsureSessionsDir();
};

}  // namespace aicode
