// Copyright 2026 Prosophor Contributors
// SPDX-License-Identifier: Apache-2.0

#include "background_run_tool.h"

#include "managers/background_task_manager.h"
#include "common/log_wrapper.h"

namespace prosophor {

BackgroundRunTool& BackgroundRunTool::GetInstance() {
    static BackgroundRunTool instance;
    return instance;
}

std::string BackgroundRunTool::Execute(const std::string& action, const nlohmann::json& params) {
    try {
        if (action == "run") {
            return Run(params);
        } else if (action == "get") {
            return Get(params);
        } else if (action == "list") {
            return List(params);
        } else if (action == "cancel") {
            return Cancel(params);
        } else if (action == "drain") {
            return Drain(params);
        } else {
            return R"({"error": "Unknown action. Use: run, get, list, cancel, drain"})";
        }
    } catch (const std::exception& e) {
        LOG_ERROR("BackgroundRunTool {} failed: {}", action, e.what());
        return R"({"error": ")" + std::string(e.what()) + R"("})";
    }
}

std::string BackgroundRunTool::Run(const nlohmann::json& params) {
    auto& bg_mgr = BackgroundTaskManager::GetInstance();

    std::string command = params.value("command", "");
    std::string cwd = params.value("cwd", "");

    if (command.empty()) {
        return R"({"error": "command is required"})";
    }

    std::string task_id = bg_mgr.RunInDir(command, cwd);

    nlohmann::json result = {
        {"success", true},
        {"task_id", task_id},
        {"command", command},
        {"cwd", cwd},
        {"message", "Background task started. Use 'drain' to check for results."}
    };

    return result.dump(2);
}

std::string BackgroundRunTool::Get(const nlohmann::json& params) {
    auto& bg_mgr = BackgroundTaskManager::GetInstance();

    std::string task_id = params.value("task_id", "");
    if (task_id.empty()) {
        return R"({"error": "task_id is required"})";
    }

    try {
        auto task = bg_mgr.GetTask(task_id);

        nlohmann::json result = {
            {"task_id", task.id},
            {"command", task.command},
            {"status", task.status},
            {"result", task.result},
            {"started_at", task.started_at},
            {"completed_at", task.completed_at}
        };

        return result.dump(2);
    } catch (const std::exception& e) {
        return R"({"error": ")" + std::string(e.what()) + R"("})";
    }
}

std::string BackgroundRunTool::List(const nlohmann::json& /*params*/) {
    auto& bg_mgr = BackgroundTaskManager::GetInstance();

    auto tasks = bg_mgr.ListTasks();

    nlohmann::json result = nlohmann::json::array();
    for (const auto& [id, task] : tasks) {
        nlohmann::json entry = {
            {"task_id", task.id},
            {"command", task.command},
            {"status", task.status},
            {"started_at", task.started_at},
            {"completed_at", task.completed_at}
        };
        result.push_back(entry);
    }

    nlohmann::json response = {
        {"tasks", result},
        {"count", result.size()}
    };

    return response.dump(2);
}

std::string BackgroundRunTool::Cancel(const nlohmann::json& params) {
    auto& bg_mgr = BackgroundTaskManager::GetInstance();

    std::string task_id = params.value("task_id", "");
    if (task_id.empty()) {
        return R"({"error": "task_id is required"})";
    }

    bool success = bg_mgr.Cancel(task_id);

    nlohmann::json result = {
        {"success", success},
        {"task_id", task_id},
        {"message", success ? "Task cancelled" : "Task not found or already completed"}
    };

    return result.dump(2);
}

std::string BackgroundRunTool::Drain(const nlohmann::json& /*params*/) {
    auto& bg_mgr = BackgroundTaskManager::GetInstance();

    std::string notifications = bg_mgr.DrainNotifications();

    nlohmann::json result;
    result["notifications"] = nlohmann::json::parse(notifications);

    return result.dump(2);
}

}  // namespace prosophor
