// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "managers/worktree_manager.h"

#include <chrono>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <cstdlib>

#include "common/log_wrapper.h"
#include "common/config.h"

namespace aicode {

namespace fs = std::filesystem;

WorktreeManager& WorktreeManager::GetInstance() {
    static WorktreeManager instance;
    return instance;
}

void WorktreeManager::Initialize(const std::string& base_dir) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (base_dir.empty()) {
        base_dir_ = (AiCodeConfig::BaseDir() / ".worktrees").string();
    } else {
        base_dir_ = base_dir;
    }

    index_path_ = base_dir_ + "/index.json";
    events_path_ = base_dir_ + "/events.jsonl";

    // Create directories if needed
    fs::create_directories(base_dir_);

    // Load existing index
    LoadIndex();

    LOG_INFO("WorktreeManager initialized: {}", base_dir_);
}

std::string WorktreeManager::GetCurrentBranch() const {
    try {
        std::string cmd = "cd " + fs::current_path().string() + " && git rev-parse --abbrev-ref HEAD";
        std::string result;
        char buffer[256];
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "main";

        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);

        // Trim whitespace
        size_t end = result.find_last_not_of(" \n\r\t");
        if (end != std::string::npos) {
            result = result.substr(0, end + 1);
        }
        return result.empty() ? "main" : result;
    } catch (...) {
        return "main";
    }
}

std::string WorktreeManager::GetWorktreeDir(const std::string& name) const {
    return base_dir_ + "/" + name;
}

std::string WorktreeManager::GetWorktreeBranchName(const std::string& name) const {
    return "wt/" + name;
}

std::string WorktreeManager::RunGitCommand(const std::vector<std::string>& args,
                                            const std::string& cwd) const {
    std::string cmd = "git";
    for (const auto& arg : args) {
        cmd += " " + arg;
    }

    std::string full_cmd = cmd;
    if (!cwd.empty()) {
        full_cmd = "cd " + cwd + " && " + cmd;
    }

    std::string result;
    char buffer[256];
    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to execute git command: " + cmd);
    }

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }

    int status = pclose(pipe);
    if (status != 0) {
        throw std::runtime_error("Git command failed with status " + std::to_string(status));
    }

    return result;
}

void WorktreeManager::LoadIndex() {
    try {
        if (!fs::exists(index_path_)) {
            return;
        }

        std::ifstream file(index_path_);
        nlohmann::json json;
        file >> json;

        for (const auto& [name, entry] : json.items()) {
            WorktreeInfo info;
            info.name = name;
            info.path = entry.value("path", "");
            info.branch = entry.value("branch", "");
            info.task_id = entry.value("task_id", "");
            info.is_active = entry.value("is_active", true);
            info.created_at = entry.value("created_at", int64_t(0));
            info.head_commit = entry.value("head_commit", "");
            info.is_locked = entry.value("is_locked", false);
            info.locked_reason = entry.value("locked_reason", "");
            worktrees_[name] = info;
        }

        LOG_INFO("Loaded {} worktrees from index", worktrees_.size());
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load worktree index: {}", e.what());
    }
}

void WorktreeManager::SaveIndex() const {
    try {
        nlohmann::json json;
        for (const auto& [name, info] : worktrees_) {
            json[name]["path"] = info.path;
            json[name]["branch"] = info.branch;
            json[name]["task_id"] = info.task_id;
            json[name]["is_active"] = info.is_active;
            json[name]["created_at"] = info.created_at;
            json[name]["head_commit"] = info.head_commit;
            json[name]["is_locked"] = info.is_locked;
            json[name]["locked_reason"] = info.locked_reason;
        }

        std::ofstream file(index_path_);
        file << json.dump(2);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save worktree index: {}", e.what());
    }
}

WorktreeInfo WorktreeManager::Create(const std::string& name,
                                      const std::string& task_id,
                                      const std::string& base_branch) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if already exists
    if (worktrees_.count(name)) {
        throw std::runtime_error("Worktree already exists: " + name);
    }

    std::string branch = base_branch.empty() ? GetCurrentBranch() : base_branch;
    std::string wt_branch = GetWorktreeBranchName(name);
    std::string wt_path = GetWorktreeDir(name);

    try {
        // Create branch from base
        RunGitCommand({"checkout", "-b", wt_branch, branch});

        // Add worktree
        std::vector<std::string> args = {
            "worktree", "add", "-b", wt_branch, wt_path, branch
        };
        RunGitCommand(args);

        // Create worktree info
        WorktreeInfo info;
        info.name = name;
        info.path = wt_path;
        info.branch = wt_branch;
        info.task_id = task_id;
        info.is_active = true;
        info.created_at = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        // Get HEAD commit
        try {
            info.head_commit = RunGitCommand({"rev-parse", "HEAD"}, wt_path);
            size_t newline = info.head_commit.find('\n');
            if (newline != std::string::npos) {
                info.head_commit = info.head_commit.substr(0, newline);
            }
        } catch (...) {
            info.head_commit = "";
        }

        worktrees_[name] = info;
        SaveIndex();

        // Emit event
        std::ofstream events(events_path_, std::ios::app);
        nlohmann::json event;
        event["event"] = "worktree.create.after";
        event["worktree"]["name"] = name;
        event["worktree"]["path"] = wt_path;
        event["task"]["id"] = task_id;
        event["ts"] = info.created_at;
        events << event.dump() << "\n";

        LOG_INFO("Created worktree: {} -> {} (branch: {})", name, wt_path, wt_branch);
        return info;

    } catch (const std::exception& e) {
        // Emit failure event
        std::ofstream events(events_path_, std::ios::app);
        nlohmann::json event;
        event["event"] = "worktree.create.failed";
        event["worktree"]["name"] = name;
        event["error"] = e.what();
        events << event.dump() << "\n";

        LOG_ERROR("Failed to create worktree {}: {}", name, e.what());
        throw;
    }
}

bool WorktreeManager::Remove(const std::string& name, bool force, bool complete_task) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = worktrees_.find(name);
    if (it == worktrees_.end()) {
        LOG_WARN("Worktree not found: {}", name);
        return false;
    }

    const auto& info = it->second;
    std::string saved_task_id = info.task_id;

    try {
        // Emit before event
        std::ofstream events(events_path_, std::ios::app);
        nlohmann::json event;
        event["event"] = "worktree.remove.before";
        event["worktree"]["name"] = name;
        event["task"]["id"] = saved_task_id;
        events << event.dump() << "\n";
        events.close();

        // Remove worktree directory first if it exists
        if (fs::exists(info.path)) {
            std::vector<std::string> args = {"worktree", "remove"};
            if (force) {
                args.push_back("--force");
            }
            args.push_back(info.path);
            RunGitCommand(args);

            // Also remove the directory if it still exists
            if (fs::exists(info.path)) {
                fs::remove_all(info.path);
            }
        }

        // Remove worktree metadata
        RunGitCommand({"worktree", "prune", "-g"});

        // Update index
        worktrees_.erase(it);
        SaveIndex();

        // Complete task if requested
        if (complete_task && !saved_task_id.empty()) {
            // Note: Task completion is handled by TaskManager
            // This is just a notification point
            LOG_INFO("Worktree {} removed, task {} marked for completion", name, saved_task_id);
        }

        // Emit after event
        events.open(events_path_, std::ios::app);
        event["event"] = "worktree.remove.after";
        event["worktree"]["name"] = name;
        event["worktree"]["status"] = "removed";
        if (complete_task) {
            event["task"]["status"] = "completed";
        }
        events << event.dump() << "\n";

        LOG_INFO("Removed worktree: {}", name);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to remove worktree {}: {}", name, e.what());

        // Emit failure event
        std::ofstream events(events_path_, std::ios::app);
        nlohmann::json event;
        event["event"] = "worktree.remove.failed";
        event["worktree"]["name"] = name;
        event["error"] = e.what();
        events << event.dump() << "\n";

        return false;
    }
}

bool WorktreeManager::Keep(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = worktrees_.find(name);
    if (it == worktrees_.end()) {
        return false;
    }

    // Just mark as kept (not removing the worktree reference)
    // The worktree persists and can be reused
    LOG_INFO("Worktree {} marked as kept", name);

    std::ofstream events(events_path_, std::ios::app);
    nlohmann::json event;
    event["event"] = "worktree.keep";
    event["worktree"]["name"] = name;
    events << event.dump() << "\n";

    return true;
}

WorktreeInfo WorktreeManager::GetWorktree(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = worktrees_.find(name);
    if (it == worktrees_.end()) {
        throw std::runtime_error("Worktree not found: " + name);
    }
    return it->second;
}

std::vector<WorktreeInfo> WorktreeManager::ListWorktrees() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<WorktreeInfo> result;
    for (const auto& [_, info] : worktrees_) {
        result.push_back(info);
    }
    return result;
}

std::string WorktreeManager::GetWorktreePath(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = worktrees_.find(name);
    if (it == worktrees_.end()) {
        return "";
    }
    return it->second.path;
}

std::string WorktreeManager::GetWorktreeByTaskId(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& [name, info] : worktrees_) {
        if (info.task_id == task_id && info.is_active) {
            return name;
        }
    }
    return "";
}

void WorktreeManager::BindTask(const std::string& worktree_name, const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = worktrees_.find(worktree_name);
    if (it == worktrees_.end()) {
        throw std::runtime_error("Worktree not found: " + worktree_name);
    }

    it->second.task_id = task_id;
    SaveIndex();

    LOG_INFO("Bound worktree {} to task {}", worktree_name, task_id);
}

void WorktreeManager::UnbindTask(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [name, info] : worktrees_) {
        if (info.task_id == task_id) {
            info.task_id.clear();
            SaveIndex();
            LOG_INFO("Unbound task {} from worktree {}", task_id, name);
            return;
        }
    }
}

std::string WorktreeManager::ExecInWorktree(const std::string& worktree_name,
                                             const std::string& command,
                                             int timeout_seconds) {
    std::string path = GetWorktreePath(worktree_name);
    if (path.empty()) {
        throw std::runtime_error("Worktree not found: " + worktree_name);
    }

    // Execute command in worktree directory
    std::string cmd = "cd " + path + " && " + command;

    // Use timeout
    cmd += " & pid=$!; sleep " + std::to_string(timeout_seconds) +
           " && kill $pid 2>/dev/null; wait $pid 2>/dev/null";

    std::string result;
    char buffer[4096];
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to execute command in worktree");
    }

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }

    int status = pclose(pipe);
    return result;
}

std::string WorktreeManager::SwitchWorktree(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = worktrees_.find(name);
    if (it == worktrees_.end()) {
        LOG_WARN("Worktree not found: {}", name);
        return "";
    }

    const auto& info = it->second;

    // Check if worktree directory exists
    if (!fs::exists(info.path)) {
        LOG_ERROR("Worktree path does not exist: {}", info.path);
        return "";
    }

    // Switch by changing to worktree directory
    // This is a no-op in the manager itself, but returns the path
    // The caller is expected to change directory
    LOG_INFO("Switched to worktree: {} -> {}", name, info.path);
    return info.path;
}

int WorktreeManager::PruneStaleWorktrees() {
    std::lock_guard<std::mutex> lock(mutex_);

    int pruned = 0;
    std::vector<std::string> to_remove;

    // Find worktrees whose directories no longer exist
    for (const auto& [name, info] : worktrees_) {
        if (!fs::exists(info.path)) {
            to_remove.push_back(name);
        }
    }

    // Remove stale entries
    for (const auto& name : to_remove) {
        worktrees_.erase(name);
        pruned++;
        LOG_INFO("Pruned stale worktree: {}", name);
    }

    if (pruned > 0) {
        SaveIndex();
    }

    return pruned;
}

std::string WorktreeManager::ExtractWorktreeName(const std::string& path) const {
    // Extract worktree name from path (last component of path)
    try {
        fs::path p(path);
        return p.filename().string();
    } catch (...) {
        return path;
    }
}


}  // namespace aicode
