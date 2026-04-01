// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "managers/session_manager.h"

#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <algorithm>

#include "common/log_wrapper.h"
#include "common/constants.h"

namespace aicode {

SessionManager& SessionManager::GetInstance() {
    static SessionManager instance;
    return instance;
}

void SessionManager::Initialize(const std::string& sessions_dir) {
    sessions_dir_ = sessions_dir;
    EnsureSessionsDir();

    // Expand home directory
    if (sessions_dir_.find("~") == 0) {
        const char* home = getenv("HOME");
        if (home) {
            sessions_dir_ = std::string(home) + sessions_dir_.substr(1);
        }
    }

    LOG_INFO("SessionManager initialized: {}", sessions_dir_);
}

void SessionManager::EnsureSessionsDir() {
    // Create directory if it doesn't exist
    std::string dir = sessions_dir_;
    if (dir.find("~") == 0) {
        const char* home = getenv("HOME");
        if (home) {
            dir = std::string(home) + dir.substr(1);
        }
    }

    std::string cmd = "mkdir -p \"" + dir + "\"";
    system(cmd.c_str());
}

std::string SessionManager::GenerateSessionId() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    std::ostringstream oss;
    oss << "sess_" << millis;
    return oss.str();
}

std::string SessionManager::GetTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_r(&time_t_now, &tm_now);

    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string SessionManager::GetSessionPath(const std::string& session_id) const {
    std::string dir = sessions_dir_;
    if (dir.find("~") == 0) {
        const char* home = getenv("HOME");
        if (home) {
            dir = std::string(home) + dir.substr(1);
        }
    }
    return dir + "/" + session_id + ".json";
}

int SessionManager::CountTokens(const std::vector<MessageSchema>& messages) const {
    int total = 0;
    for (const auto& msg : messages) {
        for (const auto& block : msg.content) {
            if (block.type == "text" || block.type == "thinking") {
                total += block.text.size() / kCharsPerTokenEstimate;
            } else if (block.type == "tool_use") {
                total += (block.name.size() + block.input.dump().size()) / kCharsPerTokenEstimate;
            } else if (block.type == "tool_result") {
                total += block.content.size() / kCharsPerTokenEstimate;
            }
        }
    }
    return total;
}

std::string SessionManager::StartSession(const std::string& workspace) {
    current_session_id_ = GenerateSessionId();
    current_messages_.clear();
    current_metadata_ = nlohmann::json::object();
    current_metadata_["workspace"] = workspace;
    current_metadata_["created_at"] = GetTimestamp();
    current_metadata_["updated_at"] = current_metadata_["created_at"];

    LOG_INFO("Started new session: {}", current_session_id_);
    return current_session_id_;
}

void SessionManager::SaveCurrentSession() {
    if (current_session_id_.empty()) {
        StartSession();
    }

    SaveSession(current_session_id_, current_messages_, current_metadata_);
}

void SessionManager::SaveSession(const std::string& session_id,
                                  const std::vector<MessageSchema>& messages,
                                  const nlohmann::json& metadata) {
    EnsureSessionsDir();

    nlohmann::json json = SessionToJson(messages, metadata);

    std::string path = GetSessionPath(session_id);
    std::ofstream file(path);
    if (file.is_open()) {
        file << json.dump(2);
        file.close();
        LOG_INFO("Saved session {} ({} messages)", session_id, messages.size());
    } else {
        LOG_ERROR("Failed to save session to {}", path);
    }
}

bool SessionManager::LoadSession(const std::string& session_id,
                                  std::vector<MessageSchema>& messages,
                                  nlohmann::json& metadata) {
    std::string path = GetSessionPath(session_id);

    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Session not found: {}", session_id);
        return false;
    }

    try {
        nlohmann::json json;
        file >> json;
        SessionFromJson(json, messages, metadata);
        LOG_INFO("Loaded session {} ({} messages)", session_id, messages.size());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse session file: {}", e.what());
        return false;
    }
}

std::vector<SessionInfo> SessionManager::ListSessions() const {
    std::vector<SessionInfo> sessions;

    std::string dir = sessions_dir_;
    if (dir.find("~") == 0) {
        const char* home = getenv("HOME");
        if (home) {
            dir = std::string(home) + dir.substr(1);
        }
    }

    // Use find command to list session files
    std::string cmd = "find \"" + dir + "\" -name \"sess_*.json\" -type f 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return sessions;
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string filepath(buffer);
        // Remove trailing newline
        filepath.erase(filepath.find_last_not_of(" \n\r\t") + 1);

        // Extract session ID from filename
        size_t last_slash = filepath.find_last_of('/');
        std::string filename = (last_slash != std::string::npos)
            ? filepath.substr(last_slash + 1)
            : filepath;

        if (filename.size() > 5 && filename.back() == 'n' && filename.back() == 'n') {
            // Remove .json extension
            std::string session_id = filename.substr(0, filename.size() - 5);

            try {
                SessionInfo info = GetSessionInfo(session_id);
                if (!info.session_id.empty()) {
                    sessions.push_back(info);
                }
            } catch (...) {
                // Skip invalid session files
            }
        }
    }
    pclose(pipe);

    // Sort by updated_at (most recent first)
    std::sort(sessions.begin(), sessions.end(),
              [](const SessionInfo& a, const SessionInfo& b) {
                  return a.updated_at > b.updated_at;
              });

    return sessions;
}

SessionInfo SessionManager::GetSessionInfo(const std::string& session_id) const {
    SessionInfo info;
    info.session_id = session_id;

    std::string path = GetSessionPath(session_id);

    std::ifstream file(path);
    if (!file.is_open()) {
        return info;
    }

    try {
        nlohmann::json json;
        file >> json;

        info.created_at = json.value("created_at", "");
        info.updated_at = json.value("updated_at", "");
        info.workspace = json.value("workspace", "");
        info.message_count = json.value("message_count", 0);
        info.token_count = json.value("token_count", 0);
        info.cost_usd = json.value("cost_usd", 0);
        info.last_user_message = json.value("last_user_message", "");

        return info;
    } catch (...) {
        return info;
    }
}

bool SessionManager::DeleteSession(const std::string& session_id) {
    std::string path = GetSessionPath(session_id);

    std::string cmd = "rm -f \"" + path + "\"";
    int result = system(cmd.c_str());

    if (result == 0) {
        LOG_INFO("Deleted session: {}", session_id);

        if (session_id == current_session_id_) {
            current_session_id_.clear();
            current_messages_.clear();
        }

        return true;
    }

    return false;
}

std::string SessionManager::GetLastSessionId() const {
    auto sessions = ListSessions();
    if (sessions.empty()) {
        return "";
    }
    return sessions[0].session_id;
}

bool SessionManager::ResumeLastSession(std::vector<MessageSchema>& messages, nlohmann::json& metadata) {
    std::string last_id = GetLastSessionId();
    if (last_id.empty()) {
        return false;
    }

    bool loaded = LoadSession(last_id, messages, metadata);
    if (loaded) {
        current_session_id_ = last_id;
        current_messages_ = messages;
        current_metadata_ = metadata;
    }
    return loaded;
}

void SessionManager::AddMessageToSession(const MessageSchema& msg) {
    current_messages_.push_back(msg);
    current_metadata_["updated_at"] = GetTimestamp();
    current_metadata_["message_count"] = static_cast<int>(current_messages_.size());

    // Track last user message
    if (msg.role == "user") {
        current_metadata_["last_user_message"] = msg.text();
    }

    // Auto-save every 10 messages
    if (current_messages_.size() % 10 == 0) {
        SaveCurrentSession();
    }
}

void SessionManager::UpdateSessionMetadata(const nlohmann::json& metadata) {
    for (auto& [key, value] : metadata.items()) {
        current_metadata_[key] = value;
    }
}

nlohmann::json SessionManager::SessionToJson(const std::vector<MessageSchema>& messages,
                                              const nlohmann::json& metadata) const {
    nlohmann::json json = metadata;

    json["session_id"] = current_session_id_;

    nlohmann::json messages_array = nlohmann::json::array();
    for (const auto& msg : messages) {
        nlohmann::json msg_json = nlohmann::json::object();
        msg_json["role"] = msg.role;

        nlohmann::json content_array = nlohmann::json::array();
        for (const auto& block : msg.content) {
            nlohmann::json block_json = nlohmann::json::object();
            block_json["type"] = block.type;
            if (!block.text.empty()) {
                block_json["text"] = block.text;
            }
            if (!block.tool_use_id.empty()) {
                block_json["tool_use_id"] = block.tool_use_id;
                block_json["name"] = block.name;
                block_json["input"] = block.input;
            }
            if (!block.content.empty()) {
                block_json["content"] = block.content;
                block_json["is_error"] = block.is_error;
            }
            content_array.push_back(block_json);
        }
        msg_json["content"] = content_array;
        messages_array.push_back(msg_json);
    }
    json["messages"] = messages_array;

    json["message_count"] = static_cast<int>(messages.size());
    json["token_count"] = CountTokens(messages);

    return json;
}

void SessionManager::SessionFromJson(const nlohmann::json& json,
                                      std::vector<MessageSchema>& messages,
                                      nlohmann::json& metadata) const {
    messages.clear();
    metadata = json;

    if (json.contains("messages") && json["messages"].is_array()) {
        for (const auto& msg_json : json["messages"]) {
            MessageSchema msg;
            msg.role = msg_json.value("role", "");

            if (msg_json.contains("content") && msg_json["content"].is_array()) {
                for (const auto& block_json : msg_json["content"]) {
                    ContentSchema block;
                    block.type = block_json.value("type", "");
                    block.text = block_json.value("text", "");
                    block.tool_use_id = block_json.value("tool_use_id", "");
                    block.name = block_json.value("name", "");
                    block.input = block_json.value("input", nlohmann::json::object());
                    block.content = block_json.value("content", "");
                    block.is_error = block_json.value("is_error", false);
                    msg.content.push_back(block);
                }
            }

            messages.push_back(msg);
        }
    }
}

}  // namespace aicode
