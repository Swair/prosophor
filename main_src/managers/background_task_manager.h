// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <map>
#include <queue>
#include <mutex>
#include <thread>
#include <functional>

#include <nlohmann/json.hpp>

namespace aicode {

/// Background task status
struct BackgroundTask {
    std::string id;
    std::string command;
    std::string status;  // running, completed, failed
    std::string result;
    int64_t started_at = 0;
    int64_t completed_at = 0;
};

/// BackgroundTaskManager - Manage background task execution
class BackgroundTaskManager {
public:
    static BackgroundTaskManager& GetInstance();

    /// Run a command in background
    /// @param command Shell command to execute
    /// @return Task ID
    std::string Run(const std::string& command);

    /// Run a command in background with working directory
    std::string RunInDir(const std::string& command, const std::string& cwd);

    /// Check if task is complete
    bool IsComplete(const std::string& task_id) const;

    /// Get task result (empty if not complete)
    std::string GetResult(const std::string& task_id) const;

    /// Get task status
    BackgroundTask GetTask(const std::string& task_id) const;

    /// List all tasks
    std::map<std::string, BackgroundTask> ListTasks() const;

    /// Drain notification queue (call before each LLM request)
    /// Returns JSON array of completed task notifications
    std::string DrainNotifications();

    /// Wait for all tasks to complete
    void WaitForAll(int timeout_seconds = 300);

    /// Cancel a running task
    bool Cancel(const std::string& task_id);

private:
    BackgroundTaskManager() = default;

    /// Execute task in background thread
    void ExecuteTask(const std::string& task_id, const std::string& command, const std::string& cwd);

    mutable std::mutex mutex_;
    std::map<std::string, BackgroundTask> tasks_;
    std::queue<nlohmann::json> notification_queue_;
};

}  // namespace aicode
