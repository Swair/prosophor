// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/noncopyable.h"

namespace prosophor {

/// 历史消息(JSONL 行解析结果)。
struct HistoryMessage {
    int64_t ts = 0;
    std::string role;      // "user" | "assistant" | "tool" | ...
    std::string sender;    // 发言成员用户 ID(群聊=群成员,非群 ID;可空)
    std::string content;   // 消息文本(不含 tool 内部字段)
    std::string raw_json;  // 原行(前端可透传展示附加字段)
};

/// 会话历史读取:{sessions_dir}/{role_id}[/{owner}]/{date}.jsonl(按角色分目录,
/// owner 子目录 = chat_id 按会话隔离;过渡期兼读顶层旧布局,行内 owner 字段区分)。
/// 只读,分页(before_ts + limit)。
class SessionHistory : public Noncopyable {
public:
    explicit SessionHistory(std::string sessions_dir);

    /// 读某 chat(owner_id = p2p_{uid} / group_{gid})的历史:
    /// 扫描全部角色目录的日期文件,行 owner 匹配 + ts < before_ts 过滤,
    /// 累计 limit 条,返回逆序(新 → 旧,与 WS 实时增量衔接)。
    /// before_ts <= 0 表示从最新开始。
    std::vector<HistoryMessage> ReadMessages(const std::string& owner_id, int64_t before_ts,
                                             size_t limit) const;

    /// 扫描全部 JSONL 得到"磁盘上存在的会话"(owner + 最后一条消息时间),
    /// 按最后活动时间降序。服务器重启后内存会话为空,靠它恢复历史列表。
    std::vector<std::pair<std::string, int64_t>> ListOwners() const;

private:
    std::string sessions_dir_;
};

}  // namespace prosophor
