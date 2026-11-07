// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "web_server/group_store.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "common/crypto_utils.h"
#include "common/file_utils.h"
#include "common/log_wrapper.h"
#include "common/time_wrapper.h"
#include "prosophor_core/public/core_types.h"

namespace prosophor {

GroupStore::GroupStore(std::string data_dir) : data_dir_(std::move(data_dir)) {}

std::string GroupStore::groups_path() const {
    return data_dir_ + "/groups.json";
}

bool GroupStore::Load() {
    EnsureDirectory(data_dir_);

    auto json = ReadJson(groups_path());
    if (json && json->contains("groups") && (*json)["groups"].is_array()) {
        for (const auto& item : (*json)["groups"]) {
            WebGroup group;
            group.group_id    = item.value("group_id", "");
            group.name        = item.value("name", "");
            group.description = item.value("description", "");
            group.role_id     = item.value("role_id", "default");
            group.owner_id    = item.value("owner_id", "");
            group.created_at  = item.value("created_at", "");
            if (item.contains("member_ids") && item["member_ids"].is_array()) {
                for (const auto& m : item["member_ids"]) {
                    group.member_ids.push_back(m.get<std::string>());
                }
            }
            if (!group.group_id.empty()) {
                groups_.push_back(std::move(group));
            }
        }
    } else {
        LOG_INFO("GroupStore: no groups file at {}, starting fresh", groups_path());
    }
    LOG_INFO("GroupStore: loaded {} groups", groups_.size());
    return true;
}

void GroupStore::SaveLocked() const {
    nlohmann::json json;
    auto arr = nlohmann::json::array();
    for (const auto& g : groups_) {
        arr.push_back({{"group_id", g.group_id},
                       {"name", g.name},
                       {"description", g.description},
                       {"role_id", g.role_id},
                       {"owner_id", g.owner_id},
                       {"member_ids", g.member_ids},
                       {"created_at", g.created_at}});
    }
    json["groups"] = arr;
    WriteJson(groups_path(), json);
}

std::optional<WebGroup> GroupStore::FindLocked(const std::string& group_id) const {
    auto it = std::find_if(groups_.begin(), groups_.end(),
                           [&](const WebGroup& g) { return g.group_id == group_id; });
    if (it == groups_.end()) return std::nullopt;
    return *it;
}

std::optional<std::string> GroupStore::CreateGroup(const std::string& name,
                                                   const std::string& description,
                                                   const std::string& role_id,
                                                   const std::string& owner_id) {
    std::string clean_name = name;
    // 群名宽松校验:非空、≤64 字符
    if (clean_name.empty() || clean_name.size() > 64) {
        return std::nullopt;
    }
    if (owner_id.empty()) {
        return std::nullopt;
    }

    std::lock_guard<std::shared_mutex> lock(mutex_);
    WebGroup group;
    group.group_id = "g_" + GenerateUuid().substr(0, 16);
    group.name = clean_name;
    group.description = description;
    group.role_id = role_id.empty() ? "default" : role_id;
    group.owner_id = owner_id;
    group.member_ids.push_back(owner_id);
    group.created_at = SystemClock::GetCurrentTimestamp();
    groups_.push_back(group);
    SaveLocked();
    LOG_INFO("GroupStore: created group '{}' ({}) owner={}", group.name, group.group_id,
             owner_id);
    return group.group_id;
}

bool GroupStore::DeleteGroup(const std::string& group_id, const std::string& requester_id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    auto it = std::find_if(groups_.begin(), groups_.end(),
                           [&](const WebGroup& g) { return g.group_id == group_id; });
    if (it == groups_.end() || it->owner_id != requester_id) {
        return false;
    }
    groups_.erase(it);
    SaveLocked();
    LOG_INFO("GroupStore: deleted group {}", group_id);
    return true;
}

std::optional<WebGroup> GroupStore::GetGroup(const std::string& group_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return FindLocked(group_id);
}

std::vector<WebGroup> GroupStore::ListGroupsForUser(const std::string& user_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<WebGroup> result;
    for (const auto& g : groups_) {
        if (g.owner_id == user_id ||
            std::find(g.member_ids.begin(), g.member_ids.end(), user_id) != g.member_ids.end()) {
            result.push_back(g);
        }
    }
    return result;
}

bool GroupStore::AddMembers(const std::string& group_id, const std::string& requester_id,
                            const std::vector<std::string>& user_ids) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    auto it = std::find_if(groups_.begin(), groups_.end(),
                           [&](const WebGroup& g) { return g.group_id == group_id; });
    if (it == groups_.end() || it->owner_id != requester_id) {
        return false;
    }
    bool changed = false;
    for (const auto& uid : user_ids) {
        if (uid.empty()) continue;
        if (std::find(it->member_ids.begin(), it->member_ids.end(), uid) ==
            it->member_ids.end()) {
            it->member_ids.push_back(uid);
            changed = true;
        }
    }
    if (changed) {
        SaveLocked();
    }
    return true;
}

bool GroupStore::JoinGroup(const std::string& group_id, const std::string& user_id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    auto it = std::find_if(groups_.begin(), groups_.end(),
                           [&](const WebGroup& g) { return g.group_id == group_id; });
    if (it == groups_.end()) {
        return false;
    }
    if (std::find(it->member_ids.begin(), it->member_ids.end(), user_id) !=
        it->member_ids.end()) {
        return true;  // 已是成员
    }
    it->member_ids.push_back(user_id);
    SaveLocked();
    LOG_INFO("GroupStore: user {} joined group {}", user_id, group_id);
    return true;
}

bool GroupStore::LeaveGroup(const std::string& group_id, const std::string& user_id) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    auto it = std::find_if(groups_.begin(), groups_.end(),
                           [&](const WebGroup& g) { return g.group_id == group_id; });
    if (it == groups_.end()) {
        return false;
    }
    if (it->owner_id == user_id) {
        return false;  // owner 不可退(可删群)
    }
    auto mit = std::find(it->member_ids.begin(), it->member_ids.end(), user_id);
    if (mit == it->member_ids.end()) {
        return false;
    }
    it->member_ids.erase(mit);
    SaveLocked();
    LOG_INFO("GroupStore: user {} left group {}", user_id, group_id);
    return true;
}

bool GroupStore::IsMember(const std::string& group_id, const std::string& user_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto group = FindLocked(group_id);
    if (!group) return false;
    return std::find(group->member_ids.begin(), group->member_ids.end(), user_id) !=
           group->member_ids.end();
}

bool GroupStore::IsOwner(const std::string& group_id, const std::string& user_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto group = FindLocked(group_id);
    return group && group->owner_id == user_id;
}

std::vector<std::string> GroupStore::MemberIds(const std::string& group_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto group = FindLocked(group_id);
    if (!group) return {};
    return group->member_ids;
}

}  // namespace prosophor
