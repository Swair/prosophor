// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>

#include <nlohmann/json.hpp>

namespace aicode {

/// Worktree metadata
struct WorktreeInfo {
    std::string name;
    std::string path;
    std::string branch;
    std::string task_id;
    bool is_active = true;
    int64_t created_at = 0;
    std::string head_commit;
    bool is_locked = false;
    std::string locked_reason;
};

/// WorktreeManager - Manage git worktrees for task isolation
class WorktreeManager {
public:
    static WorktreeManager& GetInstance();

    /// Create a new worktree bound to a task
    /// @param name Worktree name (usually matches task ID or slug)
    /// @param task_id Associated task ID
    /// @param base_branch Base branch to create from (default: current branch)
    /// @return WorktreeInfo or throws on error
    WorktreeInfo Create(const std::string& name,
                        const std::string& task_id = "",
                        const std::string& base_branch = "");

    /// Remove a worktree
    /// @param name Worktree name
    /// @param force Force removal even with uncommitted changes
    /// @param complete_task If true and has task_id, mark task completed
    /// @return true on success
    bool Remove(const std::string& name, bool force = false, bool complete_task = false);

    /// Keep a worktree (mark as persistent)
    bool Keep(const std::string& name);

    /// Get worktree info
    WorktreeInfo GetWorktree(const std::string& name) const;

    /// List all worktrees
    std::vector<WorktreeInfo> ListWorktrees() const;

    /// Get worktree path by name
    std::string GetWorktreePath(const std::string& name) const;

    /// Get worktree by task ID
    std::string GetWorktreeByTaskId(const std::string& task_id) const;

    /// Bind a task to a worktree
    void BindTask(const std::string& worktree_name, const std::string& task_id);

    /// Unbind task from worktree
    void UnbindTask(const std::string& task_id);

    /// Initialize manager (load index from disk)
    void Initialize(const std::string& base_dir = "");

    /// Execute command in worktree context
    std::string ExecInWorktree(const std::string& worktree_name,
                                const std::string& command,
                                int timeout_seconds = 300);

    /// Switch to a worktree (cd into it)
    std::string SwitchWorktree(const std::string& name);

    /// Prune stale worktrees
    int PruneStaleWorktrees();

    /// Extract worktree name from path
    std::string ExtractWorktreeName(const std::string& path) const;

private:
    WorktreeManager() = default;

    /// Run git command
    std::string RunGitCommand(const std::vector<std::string>& args,
                              const std::string& cwd = "") const;

    /// Save index to disk
    void SaveIndex() const;

    /// Load index from disk
    void LoadIndex();

    /// Get current branch name
    std::string GetCurrentBranch() const;

    /// Generate worktree directory path
    std::string GetWorktreeDir(const std::string& name) const;

    /// Generate branch name for worktree
    std::string GetWorktreeBranchName(const std::string& name) const;

    mutable std::mutex mutex_;
    std::map<std::string, WorktreeInfo> worktrees_;
    std::string base_dir_;  // .worktrees directory
    std::string index_path_;
    std::string events_path_;
};

}  // namespace aicode
