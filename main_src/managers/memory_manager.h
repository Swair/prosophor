// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/log_wrapper.h"

#include "common/config.h"

namespace aicode {

/// Manages workspace files and daily memory storage
class MemoryManager {
 public:
    explicit MemoryManager(const std::filesystem::path& workspace_path);
    ~MemoryManager();

    /// Load all workspace files into memory
    void LoadWorkspaceFiles();

    /// Read identity files (SOUL.md, USER.md, MEMORY.md)
    std::string ReadIdentityFile(const std::string& filename) const;

    /// Read AGENTS.md (behavior instructions)
    std::string ReadAgentsFile() const;

    /// Read TOOLS.md (tool usage guide)
    std::string ReadToolsFile() const;

    /// Search memory files for content
    std::vector<std::string> SearchMemory(const std::string& query) const;

    /// Save daily memory entry
    void SaveDailyMemory(const std::string& content);

    /// File change callback type
    using FileChangeCallback = std::function<void(const std::string& filename)>;

    /// Start file system watcher (polling)
    void StartFileWatcher();

    /// Stop file system watcher
    void StopFileWatcher();

    /// Set callback for file changes
    void SetFileChangeCallback(FileChangeCallback cb);

    /// Get workspace path
    const std::filesystem::path& GetWorkspacePath() const;

    /// Set workspace for a specific agent ID
    void SetAgentWorkspace(const std::string& agent_id);

    /// Get base AiCode directory (~/.aicode)
    std::filesystem::path GetBaseDir() const;

    /// Get sessions directory for an agent
    std::filesystem::path GetSessionsDir(const std::string& agent_id = "main") const;

    /// Get workspace path as string (convenience method)
    std::string GetWorkspace() const { return workspace_path_.string(); }

 private:
    bool IsMemoryFile(const std::filesystem::path& filepath) const;
    std::string ReadFileContent(const std::filesystem::path& filepath) const;
    void WriteFileContent(const std::filesystem::path& filepath,
    const std::string& content) const;

    /// Load AICODE.md files recursively from all subdirectories
    void LoadAicodeFilesRecursively(const std::filesystem::path& dir);

    std::filesystem::path workspace_path_;
    std::filesystem::path base_dir_;  // ~/.aicode
    std::string agent_id_;
    std::atomic<bool> watching_;
    std::mutex watcher_mutex_;
    FileChangeCallback change_callback_;
    std::unordered_map<std::string, std::filesystem::file_time_type> file_mtimes_;
    std::unique_ptr<std::thread> watcher_thread_;
};

}  // namespace aicode
