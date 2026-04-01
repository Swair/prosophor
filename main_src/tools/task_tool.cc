// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "tools/task_tool.h"

#include "agents/task_manager.h"
#include "common/log_wrapper.h"

namespace aicode {

std::string TaskTool::Execute(const std::string& action, const nlohmann::json& params) {
    try {
        if (action == "create") {
            return CreateTask(params);
        } else if (action == "get") {
            return GetTask(params);
        } else if (action == "update") {
            return UpdateTask(params);
        } else if (action == "list") {
            return ListTasks(params);
        } else if (action == "delete") {
            return DeleteTask(params);
        } else if (action == "claim") {
            return ClaimTask(params);
        } else if (action == "scan_unclaimed") {
            return ScanUnclaimedTasks(params);
        } else {
            return "Unknown task action: " + action;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("TaskTool {} failed: {}", action, e.what());
        return "Error: " + std::string(e.what());
    }
}

std::string TaskTool::CreateTask(const nlohmann::json& params) {
    auto& task_mgr = TaskManager::GetInstance();

    std::string subject = params.value("subject", "");
    std::string description = params.value("description", "");
    std::string active_form = params.value("active_form", "");

    if (subject.empty()) {
        return "Error: 'subject' is required";
    }

    std::string task_id = task_mgr.CreateTask(subject, description, active_form);

    nlohmann::json result = {
        {"success", true},
        {"task_id", task_id},
        {"subject", subject},
        {"message", "Task created successfully"}
    };

    return result.dump(2);
}

std::string TaskTool::GetTask(const nlohmann::json& params) {
    auto& task_mgr = TaskManager::GetInstance();

    std::string task_id = params.value("task_id", "");
    if (task_id.empty()) {
        return "Error: 'task_id' is required";
    }

    Task* task = task_mgr.GetTask(task_id);
    if (!task) {
        return "Error: Task not found: " + task_id;
    }

    nlohmann::json result = {
        {"id", task->id},
        {"subject", task->subject},
        {"description", task->description},
        {"active_form", task->active_form},
        {"status", task->StatusToString()},
        {"owner", task->owner},
        {"blocks", task->blocks},
        {"blocked_by", task->blocked_by},
        {"worktree", task->worktree},
        {"created_at", task->created_at},
        {"updated_at", task->updated_at},
        {"completed_at", task->completed_at}
    };

    if (!task->metadata.is_null()) {
        result["metadata"] = task->metadata;
    }

    return result.dump(2);
}

std::string TaskTool::UpdateTask(const nlohmann::json& params) {
    auto& task_mgr = TaskManager::GetInstance();

    std::string task_id = params.value("task_id", "");
    if (task_id.empty()) {
        return "Error: 'task_id' is required";
    }

    bool updated = false;
    std::string updates;

    // Update status
    if (params.contains("status")) {
        std::string status_str = params["status"];
        TaskStatus status = Task::FromString(status_str);
        if (task_mgr.UpdateTaskStatus(task_id, status)) {
            updated = true;
            updates += "status=" + status_str + " ";
        }
    }

    // Update owner
    if (params.contains("owner")) {
        std::string owner = params["owner"];
        if (task_mgr.UpdateTaskOwner(task_id, owner)) {
            updated = true;
            updates += "owner=" + owner + " ";
        }
    }

    // Update subject
    if (params.contains("subject")) {
        std::string subject = params["subject"];
        if (task_mgr.UpdateTaskDescription(task_id, subject)) {
            updated = true;
            updates += "subject updated ";
        }
    }

    if (!updated) {
        return "Error: No valid updates provided or task not found";
    }

    nlohmann::json result = {
        {"success", true},
        {"task_id", task_id},
        {"message", "Task updated: " + updates}
    };

    return result.dump(2);
}

std::string TaskTool::ListTasks(const nlohmann::json& params) {
    auto& task_mgr = TaskManager::GetInstance();

    std::string status_filter = params.value("status", "");
    auto tasks = status_filter.empty() ?
                 task_mgr.GetAllTasks() :
                 task_mgr.GetTasksByStatus(Task::FromString(status_filter));

    nlohmann::json result = nlohmann::json::array();
    for (const auto& task : tasks) {
        nlohmann::json task_json = {
            {"id", task.id},
            {"subject", task.subject},
            {"status", task.StatusToString()},
            {"owner", task.owner}
        };
        result.push_back(task_json);
    }

    nlohmann::json response = {
        {"tasks", result},
        {"count", static_cast<int>(tasks.size())}
    };

    return response.dump(2);
}

std::string TaskTool::DeleteTask(const nlohmann::json& params) {
    auto& task_mgr = TaskManager::GetInstance();

    std::string task_id = params.value("task_id", "");
    if (task_id.empty()) {
        return "Error: 'task_id' is required";
    }

    if (task_mgr.DeleteTask(task_id)) {
        nlohmann::json result = {
            {"success", true},
            {"task_id", task_id},
            {"message", "Task deleted successfully"}
        };
        return result.dump(2);
    }

    return "Error: Failed to delete task: " + task_id;
}

std::string TaskTool::GetTaskOutput(const nlohmann::json& params) {
    (void)params;
    // Task output tracking would require additional implementation
    // For now, return task details which serve as the "output"
    return GetTask(params);
}

std::string TaskTool::ClaimTask(const nlohmann::json& params) {
    auto& task_mgr = TaskManager::GetInstance();

    std::string task_id = params.value("task_id", "");
    std::string agent_id = params.value("agent_id", "");

    if (task_id.empty()) {
        return R"({"error": "task_id is required"})";
    }
    if (agent_id.empty()) {
        return R"({"error": "agent_id is required"})";
    }

    Task* task = task_mgr.GetTask(task_id);
    if (!task) {
        return R"({"error": "Task not found: )" + task_id + R"("})";
    }

    if (task->status != TaskStatus::Pending) {
        return R"({"error": "Task is not pending, current status: )" + task->StatusToString() + R"("})";
    }

    if (!task->owner.empty()) {
        return R"({"error": "Task already claimed by: )" + task->owner + R"("})";
    }

    // Claim the task
    task_mgr.UpdateTaskOwner(task_id, agent_id);
    task_mgr.UpdateTaskStatus(task_id, TaskStatus::InProgress);

    nlohmann::json result = {
        {"success", true},
        {"task_id", task_id},
        {"agent_id", agent_id},
        {"subject", task->subject},
        {"message", "Task claimed successfully"}
    };

    return result.dump(2);
}

std::string TaskTool::ScanUnclaimedTasks(const nlohmann::json& params) {
    auto& task_mgr = TaskManager::GetInstance();
    (void)params;

    auto tasks = task_mgr.GetAvailableTasks();

    nlohmann::json result = nlohmann::json::array();
    for (const auto& task : tasks) {
        nlohmann::json entry = {
            {"id", task.id},
            {"subject", task.subject},
            {"description", task.description},
            {"blocked_by", task.blocked_by}
        };
        result.push_back(entry);
    }

    nlohmann::json response = {
        {"unclaimed_tasks", result},
        {"count", result.size()}
    };

    return response.dump(2);
}

}  // namespace aicode
