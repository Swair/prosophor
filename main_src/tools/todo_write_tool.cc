// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0

#include "tools/todo_write_tool.h"

#include <sstream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <algorithm>

#include "common/log_wrapper.h"

namespace aicode {

namespace fs = std::filesystem;

TodoWriteTool& TodoWriteTool::GetInstance() {
    static TodoWriteTool instance;
    return instance;
}

std::string TodoWriteTool::GenerateId() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return "todo_" + std::to_string(millis);
}

std::string TodoWriteTool::GetTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_r(&time_t_now, &tm_now);

    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

std::string TodoWriteTool::StatusToString(TodoStatus status) const {
    switch (status) {
        case TodoStatus::Pending: return "pending";
        case TodoStatus::Completed: return "completed";
        case TodoStatus::Cancelled: return "cancelled";
    }
    return "unknown";
}

aicode::TodoStatus TodoWriteTool::StringToStatus(const std::string& s) const {
    if (s == "completed") return TodoStatus::Completed;
    if (s == "cancelled") return TodoStatus::Cancelled;
    return TodoStatus::Pending;
}

std::string TodoWriteTool::CreateTodo(const std::string& content, const std::string& priority) {
    // Ensure loaded
    if (!loaded_) {
        Load();
    }

    TodoItem item;
    item.id = GenerateId();
    item.content = content;
    item.status = TodoStatus::Pending;
    item.priority = priority;
    item.created_at = GetTimestamp();

    items_.push_back(item);
    LOG_INFO("Created todo: {} - {}", item.id, content);

    Save();
    return item.id;
}

bool TodoWriteTool::UpdateTodoStatus(const std::string& id, TodoStatus status) {
    if (!loaded_) {
        Load();
    }

    for (auto& item : items_) {
        if (item.id == id) {
            item.status = status;
            if (status == TodoStatus::Completed) {
                item.completed_at = GetTimestamp();
            }
            LOG_INFO("Updated todo {} status to {}", id, StatusToString(status));
            Save();
            return true;
        }
    }
    LOG_ERROR("Todo not found: {}", id);
    return false;
}

bool TodoWriteTool::DeleteTodo(const std::string& id) {
    if (!loaded_) {
        Load();
    }

    auto it = std::find_if(items_.begin(), items_.end(),
        [&id](const TodoItem& item) { return item.id == id; });

    if (it != items_.end()) {
        items_.erase(it);
        LOG_INFO("Deleted todo: {}", id);
        Save();
        return true;
    }
    return false;
}

const TodoItem* TodoWriteTool::GetTodo(const std::string& id) const {
    if (!loaded_) {
        const_cast<TodoWriteTool*>(this)->Load();
    }

    for (const auto& item : items_) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

TodoList TodoWriteTool::GetTodos() const {
    if (!loaded_) {
        const_cast<TodoWriteTool*>(this)->Load();
    }

    TodoList list;
    list.items = items_;

    for (const auto& item : items_) {
        if (item.status == TodoStatus::Pending) {
            list.pending_count++;
        } else if (item.status == TodoStatus::Completed) {
            list.completed_count++;
        }
    }

    return list;
}

std::vector<TodoItem> TodoWriteTool::GetPendingTodos() const {
    if (!loaded_) {
        const_cast<TodoWriteTool*>(this)->Load();
    }

    std::vector<TodoItem> result;
    for (const auto& item : items_) {
        if (item.status == TodoStatus::Pending) {
            result.push_back(item);
        }
    }
    return result;
}

std::vector<TodoItem> TodoWriteTool::GetCompletedTodos() const {
    if (!loaded_) {
        const_cast<TodoWriteTool*>(this)->Load();
    }

    std::vector<TodoItem> result;
    for (const auto& item : items_) {
        if (item.status == TodoStatus::Completed) {
            result.push_back(item);
        }
    }
    return result;
}

int TodoWriteTool::ClearCompleted() {
    if (!loaded_) {
        Load();
    }

    int count = 0;
    items_.erase(std::remove_if(items_.begin(), items_.end(),
        [&count](const TodoItem& item) {
            if (item.status == TodoStatus::Completed || item.status == TodoStatus::Cancelled) {
                count++;
                return true;
            }
            return false;
        }), items_.end());

    LOG_INFO("Cleared {} completed todos", count);
    Save();
    return count;
}

std::string TodoWriteTool::FormatTodos() const {
    if (!loaded_) {
        const_cast<TodoWriteTool*>(this)->Load();
    }

    std::ostringstream oss;

    // Pending todos
    oss << "## Pending\n\n";
    bool has_pending = false;
    for (const auto& item : items_) {
        if (item.status == TodoStatus::Pending) {
            has_pending = true;
            oss << "- [ ] `" << item.id << "` ";
            if (item.priority == "high") oss << "🔴 ";
            else if (item.priority == "medium") oss << "🟡 ";
            else oss << "🟢 ";
            oss << item.content << "\n";
        }
    }
    if (!has_pending) {
        oss << "*No pending tasks*\n";
    }

    // Completed todos
    oss << "\n## Completed\n\n";
    bool has_completed = false;
    for (const auto& item : items_) {
        if (item.status == TodoStatus::Completed) {
            has_completed = true;
            oss << "- [x] `" << item.id << "` " << item.content;
            if (!item.completed_at.empty()) {
                oss << " (" << item.completed_at << ")";
            }
            oss << "\n";
        }
    }
    if (!has_completed) {
        oss << "*No completed tasks*\n";
    }

    // Summary
    TodoList list = GetTodos();
    oss << "\n---\n";
    oss << "**Summary**: " << list.pending_count << " pending, "
        << list.completed_count << " completed\n";

    return oss.str();
}

bool TodoWriteTool::Load(const std::string& path) {
    if (!fs::exists(path)) {
        LOG_DEBUG("No todo file found at {}", path);
        loaded_ = true;
        return false;
    }

    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            LOG_ERROR("Failed to open todo file: {}", path);
            loaded_ = true;
            return false;
        }

        nlohmann::json json;
        ifs >> json;

        items_.clear();
        for (const auto& j : json) {
            TodoItem item;
            item.id = j.value("id", "");
            item.content = j.value("content", "");
            item.status = StringToStatus(j.value("status", "pending"));
            item.priority = j.value("priority", "medium");
            item.created_at = j.value("created_at", "");
            item.completed_at = j.value("completed_at", "");
            items_.push_back(item);
        }

        LOG_INFO("Loaded {} todos from {}", items_.size(), path);
        loaded_ = true;
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load todos: {}", e.what());
        loaded_ = true;
        return false;
    }
}

bool TodoWriteTool::Save(const std::string& path) const {
    // Create parent directory if needed
    fs::path p(path);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path());
    }

    nlohmann::json json = nlohmann::json::array();
    for (const auto& item : items_) {
        nlohmann::json j;
        j["id"] = item.id;
        j["content"] = item.content;
        j["status"] = StatusToString(item.status);
        j["priority"] = item.priority;
        j["created_at"] = item.created_at;
        j["completed_at"] = item.completed_at;
        json.push_back(j);
    }

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        LOG_ERROR("Failed to open todo file for writing: {}", path);
        return false;
    }

    ofs << json.dump(2);
    ofs.close();

    LOG_DEBUG("Saved {} todos to {}", items_.size(), path);
    return true;
}

}  // namespace aicode
