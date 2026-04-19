// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "managers/memory_manager.h"

#include <chrono>
#include <filesystem>
#include <sstream>
#include <thread>

#include "common/log_wrapper.h"
#include "common/time_wrapper.h"
#include "common/file_utils.h"

namespace aicode {

MemoryManager::MemoryManager(const std::filesystem::path& workspace_path)
    : workspace_path_(workspace_path),
      base_dir_(),
      agent_id_("default"),
      watching_(false),
      watcher_mutex_(),
      change_callback_(),
      file_mtimes_(),
      watcher_thread_() {
    if (workspace_path_.generic_string().find("/agents/") != std::string::npos) {
        base_dir_ = workspace_path_.parent_path().parent_path().parent_path();
    } else {
        base_dir_ = workspace_path_.parent_path();
    }

    std::filesystem::create_directories(workspace_path_);
    LOG_INFO("MemoryManager initialized with workspace: {}",
                  workspace_path_.string());
}

MemoryManager::~MemoryManager() { StopFileWatcher(); }

void MemoryManager::LoadWorkspaceFiles() {
    LOG_INFO("Loading workspace files from: {}", workspace_path_.string());

    // 1. Load root workspace identity files (SOUL.md, USER.md, etc.)
    for (const auto& name :
         {"SOUL.md", "USER.md", "MEMORY.md", "AGENTS.md", "TOOLS.md"}) {
        try {
            auto filepath = workspace_path_ / name;
            if (std::filesystem::exists(filepath)) {
                auto content = ReadFileContent(filepath);
                if (!content.empty()) {
                    LOG_INFO("Loaded {} ({} bytes)", name, content.size());
                }
            }
        } catch (const std::exception& e) {
            LOG_DEBUG("No {} found: {}", name, e.what());
        }
    }

    // 2. Load AICODE.md from each directory (like Claude Code's CLAUDE.md)
    // These provide directory-specific instructions/norms
    LoadAicodeFilesRecursively(workspace_path_);

    // 3. Load daily memory files
    auto memory_dir = workspace_path_ / "memory";
    if (std::filesystem::exists(memory_dir)) {
        int loaded_count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(memory_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".md") {
                try {
                    ReadFileContent(entry.path());
                    loaded_count++;
                } catch (const std::exception& e) {
                    LOG_WARN("Failed to load memory file {}: {}",
                              entry.path().filename().string(), e.what());
                }
            }
        }
        if (loaded_count > 0) {
            LOG_INFO("Loaded {} daily memory files", loaded_count);
        }
    }

    spdlog::info("Workspace files loaded successfully");
}

std::string MemoryManager::ReadIdentityFile(const std::string& filename) const {
    auto filepath = workspace_path_ / filename;
    return ReadFileContent(filepath);
}

std::string MemoryManager::ReadAgentsFile() const {
    return ReadIdentityFile("AGENTS.md");
}

std::string MemoryManager::ReadToolsFile() const {
    return ReadIdentityFile("TOOLS.md");
}

void MemoryManager::LoadAicodeFilesRecursively(const std::filesystem::path& dir) {
    // Load AICODE.md from current directory if exists
    auto aicode_file = dir / "AICODE.md";
    if (std::filesystem::exists(aicode_file)) {
        try {
            auto content = ReadFileContent(aicode_file);
            if (!content.empty()) {
                // Store with relative path prefix for context
                auto rel_path = std::filesystem::relative(dir, workspace_path_);
                std::string prefix = rel_path.empty() ? "" : ("[" + rel_path.string() + "] ");
                LOG_INFO("Loaded {}AICODE.md ({} bytes)", prefix, content.size());
            }
        } catch (const std::exception& e) {
            LOG_DEBUG("Failed to load AICODE.md from {}: {}", dir.string(), e.what());
        }
    }

    // Recursively search subdirectories (skip hidden directories and symlinks)
    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_directory() &&
                !entry.is_symlink() &&
                entry.path().filename().string()[0] != '.') {
                LoadAicodeFilesRecursively(entry.path());
            }
        }
    } catch (const std::exception& e) {
        LOG_DEBUG("Failed to iterate directory {}: {}", dir.string(), e.what());
    }
}

std::vector<std::string> MemoryManager::SearchMemory(
    const std::string& query) const {
    std::vector<std::string> results;

    std::vector<std::string> identity_files = {"SOUL.md", "USER.md", "MEMORY.md",
                                               "AGENTS.md", "TOOLS.md"};
    for (const auto& filename : identity_files) {
        try {
            auto content = ReadIdentityFile(filename);
            if (content.find(query) != std::string::npos) {
                results.push_back("File: " + filename + "\nContent: " + content);
            }
        } catch (const std::exception&) {
            // Skip files that can't be read
        }
    }

    auto memory_dir = workspace_path_ / "memory";
    if (std::filesystem::exists(memory_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(memory_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".md") {
                try {
                    auto content = ReadFileContent(entry.path());
                    if (content.find(query) != std::string::npos) {
                        results.push_back("File: " + entry.path().string() +
                                          "\nContent: " + content);
                    }
                } catch (const std::exception&) {
                }
            }
        }
    }

    return results;
}

void MemoryManager::SaveDailyMemory(const std::string& content) {
    auto date_str = SystemClock::GetCurrentDate();
    auto timestamp_str = SystemClock::GetCurrentTimestamp();

    auto memory_dir = workspace_path_ / "memory";
    std::filesystem::create_directories(memory_dir);

    std::ostringstream entry_stream;
    entry_stream << "## " << timestamp_str << "\n";
    entry_stream << content << "\n";
    auto entry_content = entry_stream.str();

    auto memory_file = memory_dir / (date_str + ".md");
    if (!WriteFile(memory_file.string(), entry_content, true)) {
        throw std::runtime_error("Failed to write to memory file: " +
                                 memory_file.string());
    }
    spdlog::debug("Saved memory entry to {}", memory_file.string());
}

void MemoryManager::StartFileWatcher() {
    if (watching_) {
        return;
    }
    watching_ = true;

    {
        std::lock_guard<std::mutex> lock(watcher_mutex_);
        for (const auto& name :
             {"SOUL.md", "USER.md", "MEMORY.md", "AGENTS.md", "TOOLS.md"}) {
            auto path = workspace_path_ / name;
            if (std::filesystem::exists(path)) {
                file_mtimes_[name] = std::filesystem::last_write_time(path);
            }
        }
    }

    watcher_thread_ = std::make_unique<std::thread>([this]() {
        while (watching_) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!watching_) {
                break;
            }

            for (const auto& name :
                 {"SOUL.md", "USER.md", "MEMORY.md", "AGENTS.md", "TOOLS.md"}) {
                auto path = workspace_path_ / name;
                if (!std::filesystem::exists(path)) {
                    continue;
                }
                auto mtime = std::filesystem::last_write_time(path);

                FileChangeCallback cb_copy;
                {
                    std::lock_guard<std::mutex> lock(watcher_mutex_);
                    auto it = file_mtimes_.find(name);
                    if (it == file_mtimes_.end() || it->second != mtime) {
                        file_mtimes_[name] = mtime;
                        LOG_INFO("File changed: {}", name);
                        cb_copy = change_callback_;
                    }
                }
                if (cb_copy) {
                    cb_copy(name);
                }
            }
        }
    });

    LOG_INFO("File watcher started for workspace: {}",
              workspace_path_.string());
}

void MemoryManager::StopFileWatcher() {
    if (!watching_) return;
    watching_ = false;
    if (watcher_thread_ && watcher_thread_->joinable()) {
        watcher_thread_->join();
    }
    watcher_thread_.reset();
    LOG_INFO("File watcher stopped");
}

void MemoryManager::SetFileChangeCallback(FileChangeCallback cb) {
    std::lock_guard<std::mutex> lock(watcher_mutex_);
    change_callback_ = std::move(cb);
}

const std::filesystem::path& MemoryManager::GetWorkspacePath() const {
    return workspace_path_;
}

void MemoryManager::SetAgentWorkspace(const std::string& agent_id) {
    agent_id_ = agent_id;
    workspace_path_ = base_dir_ / "agents" / agent_id / "workspace";
    std::filesystem::create_directories(workspace_path_);
    LOG_DEBUG("SetAgentWorkspace: {}", workspace_path_.string());
}

std::filesystem::path MemoryManager::GetBaseDir() const { return base_dir_; }

std::filesystem::path MemoryManager::GetSessionsDir(
    const std::string& agent_id) const {
    return base_dir_ / "agents" / agent_id / "sessions";
}

bool MemoryManager::IsMemoryFile(const std::filesystem::path& filepath) const {
    auto filename = filepath.filename().string();

    if (filename == "SOUL.md" || filename == "USER.md" ||
        filename == "MEMORY.md" || filename == "AGENTS.md" ||
        filename == "TOOLS.md") {
        return true;
    }

    if (filepath.parent_path().filename() == "memory" &&
        filepath.extension() == ".md") {
        return true;
    }

    return false;
}

std::string MemoryManager::ReadFileContent(
    const std::filesystem::path& filepath) const {
    auto content = ReadFile(filepath.string());
    if (!content) {
        throw std::runtime_error("File not found: " + filepath.string());
    }
    return *content;
}

void MemoryManager::WriteFileContent(const std::filesystem::path& filepath,
                                     const std::string& content) const {
    if (!WriteFile(filepath.string(), content)) {
        throw std::runtime_error("Failed to write file: " + filepath.string());
    }
}

}  // namespace aicode
