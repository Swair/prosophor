// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "agents/task_manager.h"

#include <fstream>
#include <sstream>
#include <algorithm>

#include "common/log_wrapper.h"
#include "common/constants.h"
#include "common/time_wrapper.h"
#include "common/file_utils.h"

namespace aicode {

TaskManager& TaskManager::GetInstance() {
    static TaskManager instance;
    return instance;
}

void TaskManager::Initialize(const std::string& storage_path) {
    storage_path_ = storage_path;
    LoadFromFile();
    LOG_INFO("TaskManager initialized, loaded {} tasks", tasks_.size());
}

std::string TaskManager::GenerateTaskId() {
    return "task_" + std::to_string(next_task_id_++);
}

std::string TaskManager::CreateTask(const std::string& subject,
                                     const std::string& description,
                                     const std::string& active_form) {
    Task task;
    task.id = GenerateTaskId();
    task.subject = subject;
    task.description = description;
    task.active_form = active_form.empty() ? subject : active_form;
    task.status = TaskStatus::Pending;
    task.created_at = SystemClock::GetCurrentTimeMillis();
    task.updated_at = task.created_at;

    tasks_[task.id] = task;
    SaveToFile();

    LOG_INFO("Created task: {} - {}", task.id, subject);
    return task.id;
}

Task* TaskManager::GetTask(const std::string& task_id) {
    auto it = tasks_.find(task_id);
    return (it != tasks_.end()) ? &it->second : nullptr;
}

std::vector<Task> TaskManager::GetAllTasks() const {
    std::vector<Task> result;
    result.reserve(tasks_.size());
    for (const auto& [id, task] : tasks_) {
        result.push_back(task);
    }
    return result;
}

std::vector<Task> TaskManager::GetTasksByStatus(TaskStatus status) const {
    std::vector<Task> result;
    for (const auto& [id, task] : tasks_) {
        if (task.status == status) {
            result.push_back(task);
        }
    }
    return result;
}

std::vector<Task> TaskManager::GetAvailableTasks() const {
    std::vector<Task> result;
    for (const auto& [id, task] : tasks_) {
        if (task.status == TaskStatus::Pending && CanStartTask(id)) {
            result.push_back(task);
        }
    }
    return result;
}

bool TaskManager::UpdateTaskStatus(const std::string& task_id, TaskStatus status) {
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return false;
    }

    it->second.status = status;
    it->second.updated_at = SystemClock::GetCurrentTimeMillis();

    if (status == TaskStatus::Completed || status == TaskStatus::Failed ||
        status == TaskStatus::Cancelled) {
        it->second.completed_at = it->second.updated_at;
    }

    SaveToFile();
    LOG_INFO("Updated task {} status to {}", task_id, it->second.StatusToString());
    return true;
}

bool TaskManager::UpdateTaskOwner(const std::string& task_id, const std::string& owner) {
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return false;
    }

    it->second.owner = owner;
    it->second.updated_at = SystemClock::GetCurrentTimeMillis();
    SaveToFile();
    LOG_INFO("Updated task {} owner to {}", task_id, owner);
    return true;
}

bool TaskManager::UpdateTaskDescription(const std::string& task_id, const std::string& description) {
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return false;
    }

    it->second.description = description;
    it->second.updated_at = SystemClock::GetCurrentTimeMillis();
    SaveToFile();
    LOG_INFO("Updated task {} description", task_id);
    return true;
}

bool TaskManager::SetTaskDependencies(const std::string& task_id,
                                       const std::vector<std::string>& blocks,
                                       const std::vector<std::string>& blocked_by) {
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return false;
    }

    it->second.blocks = blocks;
    it->second.blocked_by = blocked_by;
    it->second.updated_at = SystemClock::GetCurrentTimeMillis();

    // Update reverse dependencies
    for (const auto& blocked_id : blocked_by) {
        auto blocked_it = tasks_.find(blocked_id);
        if (blocked_it != tasks_.end()) {
            // Add this task to the blocking list of blocked tasks
            if (std::find(blocked_it->second.blocks.begin(),
                          blocked_it->second.blocks.end(),
                          task_id) == blocked_it->second.blocks.end()) {
                blocked_it->second.blocks.push_back(task_id);
            }
        }
    }

    SaveToFile();
    LOG_INFO("Updated task {} dependencies", task_id);
    return true;
}

bool TaskManager::DeleteTask(const std::string& task_id) {
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return false;
    }

    // Remove from other tasks' dependencies
    for (auto& [id, task] : tasks_) {
        task.blocks.erase(
            std::remove(task.blocks.begin(), task.blocks.end(), task_id),
            task.blocks.end()
        );
        task.blocked_by.erase(
            std::remove(task.blocked_by.begin(), task.blocked_by.end(), task_id),
            task.blocked_by.end()
        );
    }

    tasks_.erase(it);
    SaveToFile();
    LOG_INFO("Deleted task: {}", task_id);
    return true;
}

void TaskManager::ClearAllTasks() {
    tasks_.clear();
    next_task_id_ = 1;
    SaveToFile();
    LOG_INFO("Cleared all tasks");
}

bool TaskManager::CanStartTask(const std::string& task_id) const {
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return false;
    }

    const Task& task = it->second;

    // Check if any blocking tasks are not completed
    for (const auto& blocking_id : task.blocked_by) {
        auto blocking_it = tasks_.find(blocking_id);
        if (blocking_it != tasks_.end()) {
            if (blocking_it->second.status != TaskStatus::Completed) {
                return false;
            }
        }
    }

    return true;
}

Task* TaskManager::GetNextAvailableTask() {
    auto available = GetAvailableTasks();
    if (available.empty()) {
        return nullptr;
    }

    // Return the oldest pending task
    auto& task = available[0];
    return &tasks_[task.id];
}

int TaskManager::GetTaskCount(TaskStatus status) const {
    int count = 0;
    for (const auto& [id, task] : tasks_) {
        if (task.status == status) {
            count++;
        }
    }
    return count;
}

int TaskManager::GetTotalTaskCount() const {
    return static_cast<int>(tasks_.size());
}

nlohmann::json TaskManager::ToJson() const {
    nlohmann::json json = nlohmann::json::object();
    json["next_task_id"] = next_task_id_;

    nlohmann::json tasks_array = nlohmann::json::array();
    for (const auto& [id, task] : tasks_) {
        nlohmann::json task_json = nlohmann::json::object();
        task_json["id"] = task.id;
        task_json["subject"] = task.subject;
        task_json["description"] = task.description;
        task_json["active_form"] = task.active_form;
        task_json["status"] = task.StatusToString();
        task_json["owner"] = task.owner;
        task_json["blocks"] = task.blocks;
        task_json["blocked_by"] = task.blocked_by;
        task_json["created_at"] = task.created_at;
        task_json["updated_at"] = task.updated_at;
        task_json["completed_at"] = task.completed_at;
        task_json["metadata"] = task.metadata;
        tasks_array.push_back(task_json);
    }
    json["tasks"] = tasks_array;

    return json;
}

void TaskManager::FromJson(const nlohmann::json& json) {
    tasks_.clear();
    next_task_id_ = json.value("next_task_id", 1);

    if (json.contains("tasks") && json["tasks"].is_array()) {
        for (const auto& task_json : json["tasks"]) {
            Task task;
            task.id = task_json.value("id", "");
            task.subject = task_json.value("subject", "");
            task.description = task_json.value("description", "");
            task.active_form = task_json.value("active_form", "");
            task.status = Task::FromString(task_json.value("status", "pending"));
            task.owner = task_json.value("owner", "");
            task.blocks = task_json.value("blocks", std::vector<std::string>());
            task.blocked_by = task_json.value("blocked_by", std::vector<std::string>());
            task.created_at = task_json.value("created_at", 0);
            task.updated_at = task_json.value("updated_at", 0);
            task.completed_at = task_json.value("completed_at", 0);
            task.metadata = task_json.value("metadata", nlohmann::json::object());

            tasks_[task.id] = task;
        }
    }

    LOG_INFO("Loaded {} tasks from JSON", tasks_.size());
}

void TaskManager::SaveToFile() {
    WriteJson(storage_path_, ToJson(), 2);
    LOG_DEBUG("Tasks saved to {}", storage_path_);
}

void TaskManager::LoadFromFile() {
    auto json_opt = ReadJson(storage_path_);
    if (json_opt) {
        try {
            FromJson(*json_opt);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse tasks file: {}", e.what());
        }
    }
}

}  // namespace aicode
