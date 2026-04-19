// Copyright 2026 AiCode Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace aicode {

/// Todo item status
enum class TodoStatus {
    Pending,
    Completed,
    Cancelled
};

/// Single todo item
struct TodoItem {
    std::string id;
    std::string content;
    TodoStatus status = TodoStatus::Pending;
    std::string priority;  // "low", "medium", "high"
    std::string created_at;
    std::string completed_at;
};

/// Todo list
struct TodoList {
    std::vector<TodoItem> items;
    int pending_count = 0;
    int completed_count = 0;
};

/// TodoWriteTool implementation
class TodoWriteTool {
public:
    static TodoWriteTool& GetInstance();

    /// Create a new todo item
    /// @return Todo item ID
    std::string CreateTodo(const std::string& content, const std::string& priority = "medium");

    /// Update todo status
    bool UpdateTodoStatus(const std::string& id, TodoStatus status);

    /// Delete a todo item
    bool DeleteTodo(const std::string& id);

    /// Get all todos
    TodoList GetTodos() const;

    /// Get pending todos
    std::vector<TodoItem> GetPendingTodos() const;

    /// Get completed todos
    std::vector<TodoItem> GetCompletedTodos() const;

    /// Clear completed todos
    int ClearCompleted();

    /// Get todo by ID
    const TodoItem* GetTodo(const std::string& id) const;

    /// Format todo list as markdown
    std::string FormatTodos() const;

    /// Load todos from file
    bool Load(const std::string& path = ".aicode/todos.json");

    /// Save todos to file
    bool Save(const std::string& path = ".aicode/todos.json") const;

private:
    TodoWriteTool() = default;

    /// Generate unique ID
    std::string GenerateId() const;

    /// Status to string
    std::string StatusToString(TodoStatus status) const;

    /// String to status
    TodoStatus StringToStatus(const std::string& s) const;

    mutable std::vector<TodoItem> items_;
    mutable bool loaded_ = false;
};

}  // namespace aicode
