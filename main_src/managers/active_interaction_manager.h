// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include <chrono>

#include <nlohmann/json.hpp>

#include "managers/agent_session_manager.h"
#include "providers/llm_provider.h"

namespace aicode {

/// 主动交互任务类型
enum class ActiveInteractionType {
    /// 5 分钟后主动推荐问题
    RECOMMEND_QUESTION,
    /// 10 分钟后会话总结
    SESSION_SUMMARY
};

/// 主动交互任务状态
struct ActiveInteractionTask {
    std::string id;
    std::string session_id;
    ActiveInteractionType type;
    int64_t scheduled_at = 0;      // 计划时间（秒级时间戳）
    int64_t created_at = 0;        // 创建时间
    bool executed = false;
    std::string result;
};

/// ActiveInteractionManager - 主动多模态交互管理
/// 功能：
/// 1. 用户提问后 5 分钟，主动推荐问题
/// 2. 用户提问后 10 分钟，主动总结会话并存入 changelog
class ActiveInteractionManager {
public:
    static ActiveInteractionManager& GetInstance();

    /// 初始化
    void Initialize(AgentSessionManager* session_manager);

    /// 设置 LLM 调用回调（用于后台执行提词）
    using LlmExecuteCallback = std::function<std::string(const std::string& session_id, const std::string& prompt)>;
    void SetLlmExecuteCallback(LlmExecuteCallback cb) { llm_execute_callback_ = cb; }

    /// 监听用户提问事件（外部调用入口）
    /// @param session_id 会话 ID
    /// @param message 用户消息
    void OnUserMessage(const std::string& session_id, const std::string& message);

    /// 取消某个会话的所有待处理任务（会话结束时调用）
    void CancelSessionTasks(const std::string& session_id);

    /// 获取所有任务状态
    std::vector<ActiveInteractionTask> ListTasks() const;

    /// 获取 changelog 目录路径
    static std::string GetChangelogDir();

    /// 确保 changelog 目录存在
    static bool EnsureChangelogDir();

    ~ActiveInteractionManager();

private:
    ActiveInteractionManager() = default;

    /// 调度任务
    void ScheduleTask(const std::string& session_id, ActiveInteractionType type, int delay_seconds);

    /// 调度器循环
    void SchedulerLoop();

    /// 执行任务
    void ExecuteTask(ActiveInteractionTask& task);

    /// 生成任务 ID
    std::string GenerateTaskId() const;

    /// 执行 5 分钟推荐问题
    std::string ExecuteRecommendQuestion(const std::string& session_id);

    /// 执行 10 分钟会话总结
    std::string ExecuteSessionSummary(const std::string& session_id);

    /// 保存总结到 changelog 目录
    bool SaveToChangelog(const std::string& filename, const std::string& content);

    std::unordered_map<std::string, std::vector<std::string>> session_tasks_;  // session_id -> task_ids
    std::unordered_map<std::string, ActiveInteractionTask> all_tasks_;  // task_id -> task

    AgentSessionManager* session_manager_;  // 原始指针，避免循环依赖
    LlmExecuteCallback llm_execute_callback_;

    std::thread scheduler_thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
};

}  // namespace aicode
