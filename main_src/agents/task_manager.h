// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <nlohmann/json.hpp>

namespace aicode {

/// Task status
enum class TaskStatus {
    Pending,
    InProgress,
    Completed,
    Cancelled,
    Failed
};

/// Task definition
struct Task {
    std::string id;
    std::string subject;
    std::string description;
    std::string active_form;  // Present continuous form for spinner
    TaskStatus status = TaskStatus::Pending;
    std::string owner;  // Agent ID if assigned
    std::vector<std::string> blocks;  // Task IDs that this task blocks
    std::vector<std::string> blocked_by;  // Task IDs that block this task
    std::string worktree;  // Associated worktree name
    int64_t created_at = 0;
    int64_t updated_at = 0;
    int64_t completed_at = 0;
    nlohmann::json metadata;

    std::string StatusToString() const {
        switch (status) {
            case TaskStatus::Pending: return "pending";
            case TaskStatus::InProgress: return "in_progress";
            case TaskStatus::Completed: return "completed";
            case TaskStatus::Cancelled: return "cancelled";
            case TaskStatus::Failed: return "failed";
        }
        return "unknown";
    }

    static TaskStatus FromString(const std::string& s) {
        if (s == "pending") return TaskStatus::Pending;
        if (s == "in_progress") return TaskStatus::InProgress;
        if (s == "completed") return TaskStatus::Completed;
        if (s == "cancelled") return TaskStatus::Cancelled;
        if (s == "failed") return TaskStatus::Failed;
        return TaskStatus::Pending;
    }
};

/// Task manager for tracking and managing tasks
class TaskManager {
public:
    static TaskManager& GetInstance();

    /// Initialize task manager
    void Initialize(const std::string& storage_path = "~/.aicode/tasks.json");

    /// Create a new task
    std::string CreateTask(const std::string& subject,
                           const std::string& description,
                           const std::string& active_form = "");

    /// Get a task by ID
    Task* GetTask(const std::string& task_id);

    /// Get all tasks
    std::vector<Task> GetAllTasks() const;

    /// Get tasks by status
    std::vector<Task> GetTasksByStatus(TaskStatus status) const;

    /// Get available tasks (pending, not blocked)
    std::vector<Task> GetAvailableTasks() const;

    /// Update task status
    bool UpdateTaskStatus(const std::string& task_id, TaskStatus status);

    /// Update task owner
    bool UpdateTaskOwner(const std::string& task_id, const std::string& owner);

    /// Update task description
    bool UpdateTaskDescription(const std::string& task_id, const std::string& description);

    /// Set task dependencies
    bool SetTaskDependencies(const std::string& task_id,
                             const std::vector<std::string>& blocks,
                             const std::vector<std::string>& blocked_by);

    /// Delete a task
    bool DeleteTask(const std::string& task_id);

    /// Clear all tasks
    void ClearAllTasks();

    /// Check if a task can be started (not blocked)
    bool CanStartTask(const std::string& task_id) const;

    /// Get next available task
    Task* GetNextAvailableTask();

    /// Save tasks to file
    void SaveToFile();

    /// Load tasks from file
    void LoadFromFile();

    /// Export tasks to JSON
    nlohmann::json ToJson() const;

    /// Import tasks from JSON
    void FromJson(const nlohmann::json& json);

    /// Get task count by status
    int GetTaskCount(TaskStatus status) const;

    /// Get total task count
    int GetTotalTaskCount() const;

private:
    TaskManager() = default;
    ~TaskManager() = default;

    std::unordered_map<std::string, Task> tasks_;
    std::string storage_path_;
    int next_task_id_ = 1;

    /// Generate unique task ID
    std::string GenerateTaskId();

    /// Get current timestamp
    int64_t GetTimestamp() const;
};

}  // namespace aicode
