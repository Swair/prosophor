// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "managers/background_task_manager.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>

#include "common/log_wrapper.h"
#include "common/time_wrapper.h"

namespace aicode {

namespace fs = std::filesystem;

BackgroundTaskManager& BackgroundTaskManager::GetInstance() {
    static BackgroundTaskManager instance;
    return instance;
}

std::string BackgroundTaskManager::Run(const std::string& command) {
    return RunInDir(command, "");
}

std::string BackgroundTaskManager::RunInDir(const std::string& command, const std::string& cwd) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Generate short task ID
    std::string task_id = std::to_string(SystemClock::GetCurrentTimeMillis() % 100000000);

    // Create task entry
    BackgroundTask task;
    task.id = task_id;
    task.command = command;
    task.status = "running";
    task.started_at = SystemClock::GetCurrentTimeMillis() / 1000;

    tasks_[task_id] = task;

    // Start background thread
    std::thread(&BackgroundTaskManager::ExecuteTask, this, task_id, command, cwd).detach();

    LOG_INFO("Started background task {}: {}", task_id, command);
    return task_id;
}

void BackgroundTaskManager::ExecuteTask(const std::string& task_id, const std::string& command, const std::string& cwd) {
    std::string result;
    std::string status = "completed";

    try {
        // Build command with timeout
        std::string full_cmd = command;
        if (!cwd.empty()) {
            full_cmd = "cd " + cwd + " && " + command;
        }

        // Execute with 5 minute timeout
        full_cmd += " 2>&1";

        char buffer[4096];
        FILE* pipe = popen(full_cmd.c_str(), "r");
        if (!pipe) {
            throw std::runtime_error("Failed to execute command");
        }

        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }

        int exit_code = pclose(pipe);
        if (exit_code != 0) {
            status = "failed";
            result += "\n[Exit code: " + std::to_string(exit_code) + "]";
        }

        // Truncate result if too long
        if (result.size() > 50000) {
            result = result.substr(0, 50000) + "\n... [truncated]";
        }

    } catch (const std::exception& e) {
        status = "failed";
        result = std::string("Error: ") + e.what();
    }

    // Update task and notify
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = tasks_.find(task_id);
        if (it != tasks_.end()) {
            it->second.status = status;
            it->second.result = result;
            it->second.completed_at = SystemClock::GetCurrentTimeMillis() / 1000;

            // Add to notification queue
            nlohmann::json notification;
            notification["task_id"] = task_id;
            notification["status"] = status;
            notification["result"] = result.substr(0, 500);  // First 500 chars
            notification["command"] = it->second.command;
            notification_queue_.push(notification);
        }
    }

    LOG_INFO("Background task {} {}: {}", task_id, status, result.substr(0, 100));
}

bool BackgroundTaskManager::IsComplete(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return false;
    }
    return it->second.status != "running";
}

std::string BackgroundTaskManager::GetResult(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return "";
    }
    return it->second.result;
}

BackgroundTask BackgroundTaskManager::GetTask(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        throw std::runtime_error("Task not found: " + task_id);
    }
    return it->second;
}

std::map<std::string, BackgroundTask> BackgroundTaskManager::ListTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_;
}

std::string BackgroundTaskManager::DrainNotifications() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (notification_queue_.empty()) {
        return "[]";
    }

    nlohmann::json notifications = nlohmann::json::array();
    while (!notification_queue_.empty()) {
        notifications.push_back(notification_queue_.front());
        notification_queue_.pop();
    }

    return notifications.dump(2);
}

void BackgroundTaskManager::WaitForAll(int timeout_seconds) {
    auto start = std::chrono::steady_clock::now();

    while (true) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bool all_done = true;
            for (const auto& [id, task] : tasks_) {
                if (task.status == "running") {
                    all_done = false;
                    break;
                }
            }
            if (all_done) break;
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > timeout_seconds) {
            LOG_WARN("Timeout waiting for background tasks");
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool BackgroundTaskManager::Cancel(const std::string& task_id) {
    // Note: Cannot easily cancel a running subprocess
    // Mark as cancelled and let it finish on its own
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return false;
    }

    if (it->second.status == "running") {
        it->second.status = "cancelled";
        LOG_INFO("Cancelled background task {}", task_id);
    }
    return true;
}

}  // namespace aicode
