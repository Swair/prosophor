// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "worktree_tool.h"

#include "managers/worktree_manager.h"
#include "common/log_wrapper.h"

namespace aicode {

WorktreeTool& WorktreeTool::GetInstance() {
    static WorktreeTool instance;
    return instance;
}

std::string WorktreeTool::Execute(const std::string& action, const nlohmann::json& params) {
    try {
        if (action == "create") {
            return Create(params);
        } else if (action == "remove") {
            return Remove(params);
        } else if (action == "keep") {
            return Keep(params);
        } else if (action == "get") {
            return Get(params);
        } else if (action == "list") {
            return List(params);
        } else if (action == "exec") {
            return Exec(params);
        } else {
            return R"({"error": "Unknown worktree action. Use: create, remove, keep, get, list, exec"})";
        }
    } catch (const std::exception& e) {
        LOG_ERROR("WorktreeTool {} failed: {}", action, e.what());
        return R"({"error": ")" + std::string(e.what()) + R"("})";
    }
}

std::string WorktreeTool::Create(const nlohmann::json& params) {
    auto& wt_mgr = WorktreeManager::GetInstance();

    std::string name = params.value("name", "");
    std::string task_id = params.value("task_id", "");
    std::string base_branch = params.value("base_branch", "");

    if (name.empty()) {
        return R"({"error": "name is required"})";
    }

    auto info = wt_mgr.Create(name, task_id, base_branch);

    nlohmann::json result = {
        {"success", true},
        {"name", info.name},
        {"path", info.path},
        {"branch", info.branch},
        {"task_id", info.task_id},
        {"message", "Worktree created successfully"}
    };

    return result.dump(2);
}

std::string WorktreeTool::Remove(const nlohmann::json& params) {
    auto& wt_mgr = WorktreeManager::GetInstance();

    std::string name = params.value("name", "");
    bool force = params.value("force", false);
    bool complete_task = params.value("complete_task", false);

    if (name.empty()) {
        return R"({"error": "name is required"})";
    }

    bool success = wt_mgr.Remove(name, force, complete_task);

    nlohmann::json result = {
        {"success", success},
        {"name", name},
        {"message", success ? "Worktree removed successfully" : "Failed to remove worktree"}
    };

    return result.dump(2);
}

std::string WorktreeTool::Keep(const nlohmann::json& params) {
    auto& wt_mgr = WorktreeManager::GetInstance();

    std::string name = params.value("name", "");
    if (name.empty()) {
        return R"({"error": "name is required"})";
    }

    bool success = wt_mgr.Keep(name);

    nlohmann::json result = {
        {"success", success},
        {"name", name},
        {"message", success ? "Worktree marked as kept" : "Failed to keep worktree"}
    };

    return result.dump(2);
}

std::string WorktreeTool::Get(const nlohmann::json& params) {
    auto& wt_mgr = WorktreeManager::GetInstance();

    std::string name = params.value("name", "");
    if (name.empty()) {
        return R"({"error": "name is required"})";
    }

    try {
        auto info = wt_mgr.GetWorktree(name);

        nlohmann::json result = {
            {"name", info.name},
            {"path", info.path},
            {"branch", info.branch},
            {"task_id", info.task_id},
            {"is_active", info.is_active},
            {"created_at", info.created_at}
        };

        return result.dump(2);
    } catch (const std::exception& e) {
        return R"({"error": ")" + std::string(e.what()) + R"("})";
    }
}

std::string WorktreeTool::List(const nlohmann::json& params) {
    auto& wt_mgr = WorktreeManager::GetInstance();

    auto worktrees = wt_mgr.ListWorktrees();

    nlohmann::json result = nlohmann::json::array();
    for (const auto& wt : worktrees) {
        nlohmann::json entry = {
            {"name", wt.name},
            {"path", wt.path},
            {"branch", wt.branch},
            {"task_id", wt.task_id},
            {"is_active", wt.is_active},
            {"created_at", wt.created_at}
        };
        result.push_back(entry);
    }

    nlohmann::json response = {
        {"worktrees", result},
        {"count", result.size()}
    };

    return response.dump(2);
}

std::string WorktreeTool::Exec(const nlohmann::json& params) {
    auto& wt_mgr = WorktreeManager::GetInstance();

    std::string worktree = params.value("worktree", "");
    std::string command = params.value("command", "");
    int timeout = params.value("timeout", 300);

    if (worktree.empty()) {
        return R"({"error": "worktree is required"})";
    }
    if (command.empty()) {
        return R"({"error": "command is required"})";
    }

    try {
        std::string output = wt_mgr.ExecInWorktree(worktree, command, timeout);

        nlohmann::json result = {
            {"success", true},
            {"worktree", worktree},
            {"output", output}
        };

        return result.dump(2);
    } catch (const std::exception& e) {
        return R"({"error": ")" + std::string(e.what()) + R"("})";
    }
}

}  // namespace aicode
