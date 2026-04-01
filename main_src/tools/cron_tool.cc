// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "tools/cron_tool.h"

#include "services/cron_scheduler.h"
#include "common/log_wrapper.h"

namespace aicode {

std::string CronTool::Execute(const std::string& action, const nlohmann::json& params) {
    try {
        if (action == "create") {
            return Create(params);
        } else if (action == "list") {
            return List(params);
        } else if (action == "delete") {
            return Delete(params);
        } else if (action == "run") {
            return RunNow(params);
        } else if (action == "pause") {
            return Pause(params);
        } else if (action == "resume") {
            return Resume(params);
        } else {
            return "Unknown cron action: " + action;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("CronTool {} failed: {}", action, e.what());
        return "Error: " + std::string(e.what());
    }
}

std::string CronTool::Create(const nlohmann::json& params) {
    auto& scheduler = CronScheduler::GetInstance();

    std::string cron = params.value("cron", "");
    std::string prompt = params.value("prompt", "");
    bool recurring = params.value("recurring", true);
    bool durable = params.value("durable", false);

    if (cron.empty()) {
        return "Error: 'cron' expression is required";
    }
    if (prompt.empty()) {
        return "Error: 'prompt' is required";
    }

    std::string task_id = scheduler.Schedule(cron, prompt, recurring, durable);

    if (task_id.empty()) {
        return "Error: Failed to schedule task";
    }

    nlohmann::json result = {
        {"success", true},
        {"task_id", task_id},
        {"cron", cron},
        {"prompt", prompt},
        {"recurring", recurring},
        {"message", "Scheduled task created successfully"}
    };

    return result.dump(2);
}

std::string CronTool::List(const nlohmann::json& params) {
    (void)params;
    auto& scheduler = CronScheduler::GetInstance();

    auto tasks = scheduler.ListTasks();

    nlohmann::json result = nlohmann::json::array();
    for (const auto& task : tasks) {
        std::string status_str;
        switch (task.status) {
            case CronJobStatus::Active: status_str = "active"; break;
            case CronJobStatus::Paused: status_str = "paused"; break;
            case CronJobStatus::Completed: status_str = "completed"; break;
            case CronJobStatus::Failed: status_str = "failed"; break;
        }

        nlohmann::json task_json = {
            {"id", task.id},
            {"cron", task.cron_expression},
            {"prompt", task.prompt},
            {"status", status_str},
            {"recurring", task.recurring},
            {"durable", task.durable},
            {"execution_count", task.execution_count},
            {"created_at", task.created_at},
            {"next_execution_at", task.next_execution_at}
        };
        result.push_back(task_json);
    }

    nlohmann::json response = {
        {"tasks", result},
        {"count", static_cast<int>(tasks.size())}
    };

    return response.dump(2);
}

std::string CronTool::Delete(const nlohmann::json& params) {
    auto& scheduler = CronScheduler::GetInstance();

    std::string task_id = params.value("task_id", "");
    if (task_id.empty()) {
        return "Error: 'task_id' is required";
    }

    if (scheduler.Delete(task_id)) {
        nlohmann::json result = {
            {"success", true},
            {"task_id", task_id},
            {"message", "Task deleted successfully"}
        };
        return result.dump(2);
    }

    return "Error: Failed to delete task: " + task_id;
}

std::string CronTool::RunNow(const nlohmann::json& params) {
    auto& scheduler = CronScheduler::GetInstance();

    std::string task_id = params.value("task_id", "");
    if (task_id.empty()) {
        return "Error: 'task_id' is required";
    }

    std::string result = scheduler.RunNow(task_id);

    nlohmann::json response = {
        {"success", true},
        {"task_id", task_id},
        {"message", "Task executed", "result", result}
    };

    return response.dump(2);
}

std::string CronTool::Pause(const nlohmann::json& params) {
    auto& scheduler = CronScheduler::GetInstance();

    std::string task_id = params.value("task_id", "");
    if (task_id.empty()) {
        return "Error: 'task_id' is required";
    }

    if (scheduler.Pause(task_id)) {
        nlohmann::json result = {
            {"success", true},
            {"task_id", task_id},
            {"message", "Task paused"}
        };
        return result.dump(2);
    }

    return "Error: Failed to pause task: " + task_id;
}

std::string CronTool::Resume(const nlohmann::json& params) {
    auto& scheduler = CronScheduler::GetInstance();

    std::string task_id = params.value("task_id", "");
    if (task_id.empty()) {
        return "Error: 'task_id' is required";
    }

    if (scheduler.Resume(task_id)) {
        nlohmann::json result = {
            {"success", true},
            {"task_id", task_id},
            {"message", "Task resumed"}
        };
        return result.dump(2);
    }

    return "Error: Failed to resume task: " + task_id;
}

}  // namespace aicode
