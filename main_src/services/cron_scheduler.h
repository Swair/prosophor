// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

#include <nlohmann/json.hpp>

namespace aicode {

/// Cron job status
enum class CronJobStatus {
    Active,
    Paused,
    Completed,
    Failed
};

/// Scheduled task definition
struct ScheduledTask {
    std::string id;
    std::string cron_expression;  // Standard 5-field cron: M H DoM Mon DoW
    std::string prompt;
    std::string description;
    CronJobStatus status = CronJobStatus::Active;
    bool recurring = true;  // true = recurring, false = one-shot
    bool durable = false;   // true = persist to file
    int max_executions = 0; // 0 = unlimited
    int execution_count = 0;
    std::string created_at;
    std::string last_executed_at;
    std::string next_execution_at;
    std::string last_result;
};

/// Cron scheduler for recurring and one-shot tasks
class CronScheduler {
public:
    static CronScheduler& GetInstance();

    /// Initialize scheduler (starts background thread)
    void Initialize();

    /// Shutdown scheduler
    void Shutdown();

    /// Schedule a new task
    /// @param cron Cron expression (e.g., "*/5 * * * *" for every 5 min)
    /// @param prompt Prompt to execute
    /// @param recurring Whether to repeat or run once
    /// @param durable Whether to persist to .claude/scheduled_tasks.json
    /// @return Task ID on success, empty string on failure
    std::string Schedule(const std::string& cron,
                         const std::string& prompt,
                         bool recurring = true,
                         bool durable = false);

    /// Get task info
    ScheduledTask GetTask(const std::string& task_id) const;

    /// List all tasks
    std::vector<ScheduledTask> ListTasks() const;

    /// Pause a task
    bool Pause(const std::string& task_id);

    /// Resume a task
    bool Resume(const std::string& task_id);

    /// Delete a task
    bool Delete(const std::string& task_id);

    /// Run a task immediately
    std::string RunNow(const std::string& task_id);

    /// Parse cron expression and get next execution time
    std::string GetNextExecutionTime(const std::string& cron_expression) const;

    /// Save tasks to file
    bool SaveToFile(const std::string& path = ".claude/scheduled_tasks.json") const;

    /// Load tasks from file
    bool LoadFromFile(const std::string& path = ".claude/scheduled_tasks.json");

    /// Set callback for executing scheduled prompts
    using ExecuteCallback = std::function<std::string(const std::string& prompt)>;
    void SetExecuteCallback(ExecuteCallback cb) { execute_callback_ = cb; }

private:
    CronScheduler() = default;
    ~CronScheduler();

    /// Background scheduler loop
    void SchedulerLoop();

    /// Check if a task should run now
    bool ShouldRunNow(const ScheduledTask& task) const;

    /// Execute a task
    void ExecuteTask(ScheduledTask& task);

    /// Calculate next execution time from cron expression
    std::chrono::system_clock::time_point CalculateNextTime(const std::string& cron) const;

    /// Generate unique task ID
    std::string GenerateTaskId() const;

    /// Get current timestamp
    std::string GetTimestamp() const;

    std::unordered_map<std::string, ScheduledTask> tasks_;
    ExecuteCallback execute_callback_;
    std::thread scheduler_thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
};

}  // namespace aicode
