// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "web_server/user_store.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "common/crypto_utils.h"
#include "common/file_utils.h"
#include "common/log_wrapper.h"
#include "common/time_wrapper.h"
#include "prosophor_core/public/core_types.h"

namespace prosophor {

namespace {
constexpr int64_t kMsPerHour = 3600 * 1000;
}  // namespace

UserStore::UserStore(std::string data_dir) : data_dir_(std::move(data_dir)) {}

std::string UserStore::users_path() const {
    return data_dir_ + "/users.json";
}

std::string UserStore::tokens_path() const {
    return data_dir_ + "/tokens.json";
}

bool UserStore::Load() {
    EnsureDirectory(data_dir_);

    auto users_json = ReadJson(users_path());
    if (users_json && users_json->contains("users") && (*users_json)["users"].is_array()) {
        for (const auto& item : (*users_json)["users"]) {
            WebUser user;
            user.user_id       = item.value("user_id", "");
            user.username      = item.value("username", "");
            user.display_name  = item.value("display_name", "");
            user.salt          = item.value("salt", "");
            user.password_hash = item.value("password_hash", "");
            user.created_at    = item.value("created_at", "");
            user.last_login_at = item.value("last_login_at", "");
            if (!user.user_id.empty()) {
                users_.push_back(std::move(user));
            }
        }
    } else {
        LOG_INFO("UserStore: no users file at {}, starting fresh", users_path());
    }

    auto tokens_json = ReadJson(tokens_path());
    if (tokens_json && tokens_json->contains("tokens") && (*tokens_json)["tokens"].is_array()) {
        for (const auto& item : (*tokens_json)["tokens"]) {
            WebToken token;
            token.token         = item.value("token", "");
            token.user_id       = item.value("user_id", "");
            token.expires_at_ms = item.value("expires_at_ms", int64_t{0});
            if (!token.token.empty()) {
                tokens_.push_back(std::move(token));
            }
        }
    }
    LOG_INFO("UserStore: loaded {} users, {} tokens", users_.size(), tokens_.size());
    return true;
}

void UserStore::SaveLocked() const {
    nlohmann::json users_json;
    auto users_arr = nlohmann::json::array();
    for (const auto& u : users_) {
        users_arr.push_back({{"user_id", u.user_id},
                             {"username", u.username},
                             {"display_name", u.display_name},
                             {"salt", u.salt},
                             {"password_hash", u.password_hash},
                             {"created_at", u.created_at},
                             {"last_login_at", u.last_login_at}});
    }
    users_json["users"] = users_arr;
    WriteJson(users_path(), users_json);

    nlohmann::json tokens_json;
    auto tokens_arr = nlohmann::json::array();
    for (const auto& t : tokens_) {
        tokens_arr.push_back({{"token", t.token},
                              {"user_id", t.user_id},
                              {"expires_at_ms", t.expires_at_ms}});
    }
    tokens_json["tokens"] = tokens_arr;
    WriteJson(tokens_path(), tokens_json);
}

UserResult UserStore::RegisterUser(const std::string& username, const std::string& password,
                                   const std::string& display_name) {
    // 用户名过 SanitizeIdentity(小写 [a-z0-9_-],≤64)
    auto clean = SanitizeIdentity(username);
    if (!clean || clean->size() < 2) {
        return {false, "invalid_username", "", ""};
    }
    if (password.size() < 6) {
        return {false, "weak_password", "", ""};
    }

    std::lock_guard<std::shared_mutex> lock(mutex_);
    if (std::any_of(users_.begin(), users_.end(),
                    [&](const WebUser& u) { return u.username == *clean; })) {
        return {false, "username_taken", "", ""};
    }

    WebUser user;
    user.user_id = "u_" + GenerateUuid().substr(0, 16);
    user.username = *clean;
    user.display_name = display_name.empty() ? *clean : display_name;
    user.salt = GenerateUuid();
    user.password_hash = Sha256Hex(user.salt + password);
    user.created_at = SystemClock::GetCurrentTimestamp();
    user.last_login_at = user.created_at;

    users_.push_back(user);
    SaveLocked();
    LOG_INFO("UserStore: registered user '{}' ({})", user.username, user.user_id);
    return {true, "", "", user.user_id};
}

UserResult UserStore::LoginUser(const std::string& username, const std::string& password,
                                int token_ttl_hours) {
    std::lock_guard<std::shared_mutex> lock(mutex_);

    auto it = std::find_if(users_.begin(), users_.end(),
                           [&](const WebUser& u) { return u.username == username; });
    if (it == users_.end()) {
        return {false, "invalid_credentials", "", ""};
    }
    if (Sha256Hex(it->salt + password) != it->password_hash) {
        return {false, "invalid_credentials", "", ""};
    }

    WebToken token;
    token.token = GenerateUuid() + GenerateUuid();  // 64 hex
    token.user_id = it->user_id;
    token.expires_at_ms = SystemClock::GetCurrentTimeMillis() +
                          static_cast<int64_t>(token_ttl_hours) * kMsPerHour;
    tokens_.push_back(token);

    it->last_login_at = SystemClock::GetCurrentTimestamp();
    SaveLocked();
    LOG_INFO("UserStore: user '{}' logged in, token expires in {}h", it->username,
             token_ttl_hours);
    return {true, "", token.token, it->user_id};
}

UserResult UserStore::GetOrCreateByDeviceId(const std::string& device_id, int token_ttl_hours) {
    // device_id = 客户端生成的 UUID(32 hex),SanitizeIdentity 校验
    auto clean = SanitizeIdentity(device_id);
    if (!clean || clean->size() < 8) {
        return {false, "invalid_device_id", "", ""};
    }

    std::lock_guard<std::shared_mutex> lock(mutex_);

    // 已存在 → 复用 + 签发新 token;不存在 → 自动创建(免注册)
    auto it = std::find_if(users_.begin(), users_.end(),
                           [&](const WebUser& u) { return u.username == *clean; });
    if (it == users_.end()) {
        WebUser user;
        user.user_id = "u_" + GenerateUuid().substr(0, 16);
        user.username = *clean;
        user.display_name = "用户_" + clean->substr(0, 8);
        user.salt = GenerateUuid();
        user.password_hash = "";  // 设备身份无密码
        user.created_at = SystemClock::GetCurrentTimestamp();
        user.last_login_at = user.created_at;
        users_.push_back(user);
        it = std::find_if(users_.begin(), users_.end(),
                          [&](const WebUser& u) { return u.username == *clean; });
        LOG_INFO("UserStore: auto-created device user '{}' ({})", user.username, user.user_id);
    }

    WebToken token;
    token.token = GenerateUuid() + GenerateUuid();
    token.user_id = it->user_id;
    token.expires_at_ms = SystemClock::GetCurrentTimeMillis() +
                          static_cast<int64_t>(token_ttl_hours) * kMsPerHour;
    tokens_.push_back(token);

    it->last_login_at = SystemClock::GetCurrentTimestamp();
    SaveLocked();
    return {true, "", token.token, it->user_id};
}

bool UserStore::LogoutUser(const std::string& token) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    auto it = std::remove_if(tokens_.begin(), tokens_.end(),
                             [&](const WebToken& t) { return t.token == token; });
    if (it == tokens_.end()) {
        return false;
    }
    tokens_.erase(it, tokens_.end());
    SaveLocked();
    return true;
}

std::optional<WebUser> UserStore::ValidateToken(const std::string& token) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const int64_t now = SystemClock::GetCurrentTimeMillis();
    for (const auto& t : tokens_) {
        if (t.token == token) {
            if (t.expires_at_ms < now) {
                return std::nullopt;  // 过期
            }
            auto it = std::find_if(users_.begin(), users_.end(),
                                   [&](const WebUser& u) { return u.user_id == t.user_id; });
            if (it != users_.end()) {
                return *it;
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<WebUser> UserStore::GetUser(const std::string& user_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = std::find_if(users_.begin(), users_.end(),
                           [&](const WebUser& u) { return u.user_id == user_id; });
    if (it == users_.end()) return std::nullopt;
    return *it;
}

std::vector<WebUser> UserStore::ListUsers() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return users_;
}

}  // namespace prosophor
