// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "web_server/session_history.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "common/file_utils.h"
#include "common/log_wrapper.h"
#include "prosophor_core/public/core_types.h"

namespace prosophor {

SessionHistory::SessionHistory(std::string sessions_dir) : sessions_dir_(std::move(sessions_dir)) {}

std::vector<HistoryMessage> SessionHistory::ReadMessages(const std::string& owner_id,
                                                         int64_t before_ts,
                                                         size_t limit) const {
    std::vector<HistoryMessage> result;
    if (limit == 0 || owner_id.empty()) return result;

    // ── 收集 {role}[/{owner}]/{date}.jsonl:角色目录、日期文件均降序(新在前)──
    // 注意:ListDir/ReadDir 只返回常规文件(目录被过滤),必须直接遍历目录
    std::vector<std::string> role_names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(sessions_dir_, ec)) {
        if (entry.is_directory()) {
            role_names.push_back(entry.path().filename().string());
        }
    }
    std::sort(role_names.rbegin(), role_names.rend());

    // 单文件解析:行序即时间序(旧 → 新);owner_filter 非空时逐行匹配归属
    auto parse_file = [](const std::string& content, const std::string& owner_filter,
                         std::vector<HistoryMessage>& out) {
        size_t pos = 0;
        while (pos < content.size()) {
            size_t eol = content.find('\n', pos);
            if (eol == std::string::npos) eol = content.size();
            std::string line = content.substr(pos, eol - pos);
            pos = eol + 1;
            if (line.empty()) continue;
            try {
                auto json = nlohmann::json::parse(line);
                // owner 字段 = chat_id(EnsureSessionForChat 写入),精确匹配会话
                std::string owner = json.value("owner", std::string());
                if (!owner_filter.empty() && owner != owner_filter) continue;
                HistoryMessage msg;
                // ts 在 JSONL 里是写盘时间字符串("YYYY-MM-DD HH:MM:SS"),
                // 不是数字;类型不匹配时 value() 会抛 → 必须显式判类型
                if (json.contains("ts") && json["ts"].is_number_integer()) {
                    msg.ts = json["ts"].get<int64_t>();
                }
                msg.role = json.value("role", std::string());
                msg.sender = json.value("sender", std::string());
                msg.raw_json = line;
                if (json.contains("content") && json["content"].is_string()) {
                    msg.content = json["content"].get<std::string>();
                }
                out.push_back(std::move(msg));
            } catch (...) {
                continue;  // 跳过脏行(可重建精神)
            }
        }
    };

    std::vector<HistoryMessage> all;  // 全量候选(新 → 旧)
    const std::string safe_owner = SanitizeIdentity(owner_id).value_or("");
    for (const auto& role_name : role_names) {
        std::string role_dir = sessions_dir_ + "/" + role_name;
        if (!DirExists(role_dir)) continue;

        // Path A:新布局 {role}/{owner}/{date}.jsonl — 目录即归属,免行级过滤
        if (!safe_owner.empty()) {
            std::string owner_dir = role_dir + "/" + safe_owner;
            if (DirExists(owner_dir)) {
                auto files = ListDir(owner_dir, ".jsonl");
                std::sort(files.rbegin(), files.rend());  // 日期降序
                for (const auto& file : files) {
                    auto content = ReadFile(owner_dir + "/" + file);
                    if (!content) continue;
                    std::vector<HistoryMessage> file_msgs;
                    parse_file(*content, "", file_msgs);
                    std::reverse(file_msgs.begin(), file_msgs.end());  // 本文件内新 → 旧
                    all.insert(all.end(), file_msgs.begin(), file_msgs.end());
                }
            }
        }

        // Path B:旧布局 {role}/{date}.jsonl — 过渡期旧数据,行级 owner 过滤
        auto files = ListDir(role_dir, ".jsonl");
        std::sort(files.rbegin(), files.rend());  // 日期降序
        for (const auto& file : files) {
            auto content = ReadFile(role_dir + "/" + file);
            if (!content) continue;
            std::vector<HistoryMessage> file_msgs;
            parse_file(*content, owner_id, file_msgs);
            std::reverse(file_msgs.begin(), file_msgs.end());  // 本文件内新 → 旧
            all.insert(all.end(), file_msgs.begin(), file_msgs.end());
        }
    }

    for (const auto& msg : all) {
        if (before_ts > 0 && msg.ts >= before_ts) continue;
        result.push_back(msg);
        if (result.size() >= limit) break;
    }
    return result;
}

std::vector<std::pair<std::string, int64_t>> SessionHistory::ListOwners() const {
    // owner → 最后消息时间:JSONL ts 为 "YYYY-MM-DD HH:MM:SS" 字符串,解析为 epoch 秒
    std::unordered_map<std::string, int64_t> last_by_owner;
    std::error_code ec;

    // 单行 ts 解析(字符串 "%Y-%m-%d %H:%M:%S" → epoch 秒)
    auto parse_ts = [](const std::string& ts_str) -> int64_t {
        std::tm tm{};
        std::istringstream ss(ts_str);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        return (!ss.fail() && !ts_str.empty()) ? static_cast<int64_t>(std::mktime(&tm)) : 0;
    };

    for (const auto& role_entry : std::filesystem::directory_iterator(sessions_dir_, ec)) {
        if (!role_entry.is_directory()) continue;
        std::string role_dir = role_entry.path().string();

        // Path A:新布局 — 角色目录下的子目录即 owner,读最新文件的最后行时间
        for (const auto& owner_entry : std::filesystem::directory_iterator(role_dir, ec)) {
            if (!owner_entry.is_directory()) continue;
            std::string owner = owner_entry.path().filename().string();
            if (IsActiveOwner(owner)) continue;   // 主动触发会话不是用户会话
            auto files = ListDir(owner_entry.path().string(), ".jsonl");
            if (files.empty()) continue;  // 目录已建但尚未落盘
            // ListDir 升序 → 最后一个 = 最新日期文件
            auto content = ReadFile(owner_entry.path().string() + "/" + files.back());
            if (!content) continue;
            size_t pos = 0;
            while (pos < content->size()) {
                size_t eol = content->find('\n', pos);
                if (eol == std::string::npos) eol = content->size();
                std::string line = content->substr(pos, eol - pos);
                pos = eol + 1;
                if (line.empty()) continue;
                try {
                    auto json = nlohmann::json::parse(line);
                    int64_t ts = parse_ts(json.value("ts", std::string()));
                    last_by_owner[owner] = std::max(last_by_owner[owner], ts);
                } catch (...) {
                    continue;
                }
            }
        }

        // Path B:旧布局 — 顶层 .jsonl 逐行扫 owner + ts
        auto files = ListDir(role_dir, ".jsonl");
        for (const auto& file : files) {
            auto content = ReadFile(role_dir + "/" + file);
            if (!content) continue;
            size_t pos = 0;
            while (pos < content->size()) {
                size_t eol = content->find('\n', pos);
                if (eol == std::string::npos) eol = content->size();
                std::string line = content->substr(pos, eol - pos);
                pos = eol + 1;
                if (line.empty()) continue;
                try {
                    auto json = nlohmann::json::parse(line);
                    std::string owner = json.value("owner", std::string());
                    if (owner.empty() || IsActiveOwner(owner)) continue;
                    int64_t ts = parse_ts(json.value("ts", std::string()));
                    last_by_owner[owner] = std::max(last_by_owner[owner], ts);
                } catch (...) {
                    continue;
                }
            }
        }
    }
    std::vector<std::pair<std::string, int64_t>> result(last_by_owner.begin(),
                                                        last_by_owner.end());
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    return result;
}

}  // namespace prosophor
